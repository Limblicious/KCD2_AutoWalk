#include "RoadFollowPort.h"

#include <cmath>

#include "FootRoad.h"
#include "Log.h"
#include "NativeMagnetism.h"

namespace AutoWalk::RoadFollowPort {

void State::Reset()
{
    latched = false;
    deactivateTime = 0.0f;
    reactivateTime = 0.0f;
    hintTime = 0.0f;
    flags = 0;
    historyCount = 0;
    pathBCount = 0;
    magnetismLive = false;
    magnetYaw = 0.0f;
}

namespace {

State g_state;
bool g_latchRequest = false;
float g_sessionTime = 0.0f; // monotonically increasing session clock

constexpr float kSnapTimeDefault = 0.2f; // SnapTime window (native cvar +0x98)

float WrapPi(float a)
{
    while (a > 3.14159265f) a -= 6.28318531f;
    while (a < -3.14159265f) a += 6.28318531f;
    return a;
}

// PathHistory_PruneOlderThan (0x181ECA180): drop records older than
// now - window.
void PruneHistory(State& st, float now, float window)
{
    int kept = 0;
    for (int i = 0; i < st.historyCount; ++i) {
        if (st.historyTimes[i] >= now - window) {
            st.historyFlags[kept] = st.historyFlags[i];
            st.historyTimes[kept] = st.historyTimes[i];
            ++kept;
        }
    }
    st.historyCount = kept;
}

// HorseRoadFollow_PushPathB (0x180A4E39C): append the from/to road-point ids
// with dedup (skip when last == from-id; push to-id after from-id; avoid
// consecutive duplicates), capped at 10 entries.
void PushPathB(State& st, int fromId, int toId)
{
    if (st.pathBCount > 0 && st.pathB[st.pathBCount - 1] == fromId) {
        // skip: last == from-id
    } else if (fromId >= 0) {
        if (st.pathBCount < State::kPathCapacity) {
            st.pathB[st.pathBCount++] = fromId;
        }
    }
    if (toId >= 0 && st.pathBCount < State::kPathCapacity) {
        if (st.pathBCount == 0 || st.pathB[st.pathBCount - 1] != toId) {
            st.pathB[st.pathBCount++] = toId;
        }
    }
}

} // namespace

void SetHoldLatched(bool latched)
{
    g_latchRequest = latched;
    if (latched) {
        g_state.flags |= 0x02; // the OnPress latched bit (bit-trivial impl)
    } else {
        g_state.flags &= ~0x02;
    }
}

// PARTIAL/UNWIRED: recovered acceptance-gate scaffold based on
// S_AutoController::SetHoldLatchedImpl
// 0x1829F1C70). Any failure rejects the latch. Gate order is the recovered
// one; the horse-specific sub-gates are adapted to Henry:
//   1. move adapter valid      -> the player exists (always true in Tick)
//   2. chat-follow inactive
//   3. !latched: snap-vector test -> NOT PORTED (the cart-box entity test);
//      documented fallback: accept (the native horse would run the cart
//      proximity test against the road carts).
//   4. rider input remain gate -> Henry's manual input vs RemainAngle.
//   5. hit && !latched: enter-angle gate (min over from/to in the native;
//      the facade sample exposes yawFrom only).
//   6. path-history flick/snap -> the flick score requires the horse road
//      class (+0x7A0); the facade has none, so the score path is skipped
//      (equivalent to the native's off-road class result).
bool AcceptanceGates(State& st, const FootRoad::FootRoadProbe& sample,
                     bool hit, bool manualInput, bool chatFollow,
                     const NativeMagnetism::FrameCVars& cvars, float now)
{
    // 2. chat-follow gate.
    if (chatFollow) {
        return false;
    }

    // 4. remain gate: manual input beyond the remain tolerance rejects.
    if (manualInput) {
        return false;
    }

    // 5. enter gate (hit && !latched).
    if (hit && !st.latched) {
        const float diff = std::abs(WrapPi(sample.yawFrom - st.magnetYaw));
        if (diff * 57.2957795f > cvars.enterAngle) {
            return false;
        }
    }

    // 6. history flick/snap (recovered shape; the road-class-dependent
    //    flick scorer is inert for the facade).
    PruneHistory(st, now, cvars.snapTime > 0.0f ? cvars.snapTime : kSnapTimeDefault);
    return true;
}

// The recovered OnPress phases (S_OnPressController_Tick 0x180A4E768).
void ControllerTick(State& st, const FootRoad::FootRoadProbe& sample,
                    int phase, float dt, const NativeMagnetism::FrameCVars& cvars)
{
    switch (phase) {
    case 0: // armed: manual input returning neutral clears the armed state.
        if ((st.flags & 0x10) != 0) {
            st.flags &= ~0x10;
            st.deactivateTime = 0.0f;
            st.reactivateTime = 0.0f;
        }
        break;
    case 1: // timers + hint time countdown.
        if (st.hintTime > 0.0f) {
            st.hintTime -= dt;
            if (st.hintTime < 0.0f) st.hintTime = 0.0f;
        }
        break;
    case 2: // remain check: manual input beyond tolerance arms the timers.
        // (manual input is the caller's 'manualInput'; armed in Tick.)
        break;
    case 3: // deactivate countdown after the armed grace window.
        if (st.deactivateTime > 0.0f) {
            st.deactivateTime -= dt;
        } else if (st.reactivateTime > 0.0f) {
            st.reactivateTime -= dt;
        } else {
            st.flags &= ~0x01; // clear active
            st.flags &= ~0x02; // clear latched
        }
        break;
    case 4: // enter: a valid hit sets active.
        if (sample.hasHit) {
            st.flags |= 0x01;
        }
        break;
    case 5: // failed sample: clear active, set flick.
        st.flags &= ~0x01;
        st.flags |= 0x08;
        st.deactivateTime = 0.0f;
        st.reactivateTime = 0.0f;
        break;
    default:
        break;
    }
}

State Tick(float dt, const NativeMagnetism::FrameCVars& cvars,
           bool manualInput, bool chatFollow,
           FootRoad::FootRoadProbe& outSample)
{
    g_sessionTime += dt;
    const bool hit = FootRoad::SampleRoadStandalone(
        g_state.latched ? cvars.roadDistOn : cvars.roadDistOff, outSample);

    // Tick(0) then the acceptance.
    ControllerTick(g_state, outSample, 0, dt, cvars);

    const bool ok = hit && AcceptanceGates(g_state, outSample, hit,
                                           manualInput, chatFollow, cvars,
                                           g_sessionTime);
    if (!ok || !(g_latchRequest || g_state.latched)) {
        // Release path (the native: !hit || !ok): live off, history
        // cleared, phase 5 + 3. Also runs when the hold-E latch was never
        // requested -- a road hit alone does not engage the follow.
        g_state.magnetismLive = false;
        if (g_state.latched) {
            g_state.historyCount = 0; // pathA clear equivalent
            ControllerTick(g_state, outSample, 5, dt, cvars);
        }
        g_state.latched = false;
        g_state.flags &= ~0x02;
        ControllerTick(g_state, outSample, 3, dt, cvars);
    } else {
        if (!g_state.latched) {
            ControllerTick(g_state, outSample, 4, dt, cvars); // enter
        }
        g_state.latched = true;
        g_state.flags |= 0x02;
        ControllerTick(g_state, outSample, 2, dt, cvars);

        // Publish (HorseRoadFollow_PublishMagnetism core writes).
        g_state.magnetismLive = outSample.hasHit;
        g_state.magnetYaw = outSample.yawFrom;

        // Path history record (the recovered {flag,time} entries).
        if (g_state.historyCount < State::kHistoryCapacity) {
            g_state.historyFlags[g_state.historyCount] = g_state.flags & 0x01;
            g_state.historyTimes[g_state.historyCount] = g_sessionTime;
            ++g_state.historyCount;
        }
    }

    ControllerTick(g_state, outSample, 1, dt, cvars);

    // Manual input arms the interruption timers (phase 2's recovered
    // behavior: beyond RemainAngle -> armed + Deactivate/Reactivate times).
    if (manualInput) {
        g_state.flags |= 0x10;
        g_state.deactivateTime = cvars.deactivateTime;
        g_state.reactivateTime = cvars.reactivateTime;
    }
    ControllerTick(g_state, outSample, 3, dt, cvars);

    return g_state;
}

State GetState()
{
    return g_state;
}

} // namespace AutoWalk::RoadFollowPort
