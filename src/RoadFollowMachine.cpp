#include "RoadFollowMachine.h"

namespace AutoWalk::RoadFollowPort {

namespace {

constexpr unsigned char kBitActive = 0x01;
constexpr unsigned char kBitHit = 0x02;
constexpr unsigned char kBitSecond = 0x04;
constexpr unsigned char kBitFlick = 0x08;
constexpr unsigned char kBitArmed = 0x10;

// Phase 0 (S_OnPressController_Tick param_3==0):
// - armed + rider input inside the tolerance -> clear bit4 (interruption
//   clears on return-to-neutral);
// - chat-follow -> teardown (handled via the returned flag, applied by the
//   common path like the native cVar3).
bool Phase0(State& st, bool manualInputHeld)
{
    if ((st.flags & kBitArmed) != 0 && !manualInputHeld) {
        st.flags &= ~kBitArmed;
    }
    // The native reads the rider stick/move here for the chat-gated
    // teardown; the chat flag itself is applied by the caller.
    return true;
}

// Phase 1 (param_3==1): countdown deactivate + hint; the jump teardown.
void Phase1(State& st, float dt)
{
    st.deactivateTime = st.deactivateTime > dt ? st.deactivateTime - dt : 0.0f;
    st.hintTime = st.hintTime > dt ? st.hintTime - dt : 0.0f;
    // The native's every-10th-frame hint-visibility write (C_Player
    // +0xB08+0x110) is a game-side side effect; the game-facing wrapper
    // owns it.
}

// Phase 2 (param_3==2): reset deactivate; the manual-input threshold arms
// the interruption. Foot adaptation: WASD held = the rider stick beyond
// the remain tolerance; the native's turn/move magnitude checks collapse to
// the held boolean (no analog stick on foot).
void Phase2(State& st, bool manualInputHeld, const Cvars& cvars)
{
    st.deactivateTime = 0.0f;
    if (!manualInputHeld) {
        return; // stay: input within the remain tolerance
    }
    st.flags |= kBitArmed;
    st.deactivateTime = cvars.deactivateTime;
    st.reactivateTime = cvars.reactivateTime;
}

// Phase 3 (param_3==3): countdown reactivate while deactivate exhausted;
// at 0 -> clear bits 0 and 2 + zero the timers (sustained interruption
// deactivates).
void Phase3(State& st, float dt)
{
    if (st.deactivateTime <= 0.0f) {
        st.reactivateTime = st.reactivateTime > dt ? st.reactivateTime - dt : 0.0f;
    }
    if (st.reactivateTime > 0.0f) {
        return;
    }
    st.flags &= ~(kBitActive | kBitSecond);
    st.deactivateTime = 0.0f;
    st.reactivateTime = 0.0f;
}

// Phase 5 (param_3==5): sample failed -> clear bit0, set bit3, zero timers.
void Phase5(State& st, const SampleInput& sample)
{
    if (!sample.failed) {
        return;
    }
    st.flags &= ~kBitActive;
    st.flags |= kBitFlick;
    st.deactivateTime = 0.0f;
    st.reactivateTime = 0.0f;
}

} // namespace

void State::Reset()
{
    latched = false;
    deactivateTime = 0.0f;
    reactivateTime = 0.0f;
    hintTime = 0.0f;
    flags = 0;
    pathBCount = 0;
    magnetismLive = false;
    magnetYaw = 0.0f;
    magnetHitX = magnetHitY = magnetHitZ = 0.0f;
    fastStop = false;
}

bool HintsActive(const State& st)
{
    return (st.flags & kBitActive) != 0 &&
           !(st.deactivateTime > 0.0f) &&
           (st.flags & kBitArmed) == 0;
}

void SetActionActive(State& st, bool active)
{
    if (active) {
        st.flags |= kBitActive;
    } else {
        st.flags &= ~kBitActive;
    }
}

void PushPathB(State& st, int fromId, int toId)
{
    const auto push = [&](int id) {
        if (id < 0) {
            return;
        }
        // Dedup BEFORE the rollover: a skipped duplicate must not shrink
        // the window.
        if (st.pathBCount > 0 && st.pathB[st.pathBCount - 1] == id) {
            return;
        }
        while (st.pathBCount >= State::kPathCapacity) {
            // Rolling: drop the oldest entry.
            for (int i = 1; i < State::kPathCapacity; ++i) {
                st.pathB[i - 1] = st.pathB[i];
            }
            --st.pathBCount;
        }
        st.pathB[st.pathBCount++] = id;
    };
    // The native dedup order: skip when last == from-id, then push to-id
    // after from-id, avoiding consecutive duplicates.
    if (st.pathBCount == 0 || st.pathB[st.pathBCount - 1] != fromId) {
        push(fromId);
    }
    push(toId);
}

void StepState(State& st, const SampleInput& sample, const Cvars& cvars,
               float dt, bool manualInputHeld, bool chatFollow,
               bool jumpRequest)
{
    // Phase 0 (every tick).
    Phase0(st, manualInputHeld);

    // Slot 1 (0x180A4E98C): bit1 = the current sample hit; the return is
    // the HintsActive predicate.
    st.flags = static_cast<unsigned char>((st.flags & ~kBitHit) |
                                          (sample.hasHit ? kBitHit : 0));
    const bool ok = sample.hasHit && HintsActive(st);

    if (!ok) {
        // The common release path: live off; a latched drop runs phase 5;
        // unlatch; phase 3.
        st.magnetismLive = false;
        if (st.latched) {
            Phase5(st, sample);
        }
        st.latched = false;
        Phase3(st, dt);
    } else {
        // The common success path: phase 4 (no-op), latch, phase 2,
        // UpdateTurnParams (cart test not portable -- fastStop stays false),
        // PushPathB, Publish.
        st.latched = true;
        Phase2(st, manualInputHeld, cvars);
        PushPathB(st, sample.fromId, sample.toId);
        st.magnetismLive = sample.hasHit;
        st.magnetHitX = sample.hitX;
        st.magnetHitY = sample.hitY;
        st.magnetHitZ = sample.hitZ;
        st.magnetYaw = sample.yawFrom;
    }

    // Phase 1 (every tick): the countdowns; the chat/jump teardown clears
    // the exact native bits (0xFA = bits 0 and 2) + zeroes the timers.
    Phase1(st, dt);
    if (chatFollow || jumpRequest) {
        st.flags &= ~(kBitActive | kBitSecond);
        st.deactivateTime = 0.0f;
        st.reactivateTime = 0.0f;
    }
}

} // namespace AutoWalk::RoadFollowPort
