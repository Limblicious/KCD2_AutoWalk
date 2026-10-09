// Deterministic state-machine tests for the pure mode-1 RoadFollowPort
// (RoadFollowMachine). No game dependencies: this executable links only the
// machine translation unit. Covers the audit's required transition list
// (docs/CURRENT_IMPLEMENTATION_AUDIT.md, "Required deterministic tests").

#include "../src/RoadFollowMachine.h"
#include "../src/FollowInteractionState.h"

#include <cstdio>

using namespace AutoWalk::RoadFollowPort;
using AutoWalk::FollowInteraction::ToggleBlocker;
using AutoWalk::FollowInteraction::ToggleRequest;
using AutoWalk::FollowInteraction::ToggleResolution;

namespace {

constexpr float kDt = 1.0f / 60.0f;

Cvars DefaultCvars()
{
    Cvars c;
    c.roadDistOff = 4.0f;
    c.roadDistOn = 12.0f;
    c.remainAngle = 45.0f;
    c.deactivateTime = 0.5f;
    c.reactivateTime = 0.5f;
    return c;
}

SampleInput HitSample(int fromId = 1, int toId = 2, float yaw = 0.7f)
{
    SampleInput s;
    s.hasHit = true;
    s.failed = false;
    s.yawFrom = yaw;
    s.hitX = 10.0f;
    s.hitY = 20.0f;
    s.hitZ = 1.0f;
    s.fromId = fromId;
    s.toId = toId;
    return s;
}

SampleInput MissSample()
{
    SampleInput s;
    s.hasHit = false;
    s.failed = false;
    return s;
}

SampleInput FailedSample()
{
    SampleInput s;
    s.hasHit = false;
    s.failed = true;
    return s;
}

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        ++g_checks;                                                       \
        if (!(cond)) {                                                    \
            ++g_failures;                                                 \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
        }                                                                 \
    } while (0)

constexpr unsigned char kBitActive = 0x01;
constexpr unsigned char kBitHit = 0x02;
constexpr unsigned char kBitSecond = 0x04;
constexpr unsigned char kBitFlick = 0x08;
constexpr unsigned char kBitArmed = 0x10;

// 1. hold-E/action bit0 on + road hit -> first latch succeeds.
void TestEngage()
{
    State st;
    SetActionActive(st, true);
    StepState(st, HitSample(), DefaultCvars(), kDt, false, false, false);
    CHECK(st.latched);
    CHECK(st.magnetismLive);
    CHECK((st.flags & kBitHit) != 0);
    CHECK((st.flags & kBitActive) != 0);
}

// 2. release E after activation -> follow remains (bit0 persists without
//    further action calls).
void TestFollowPersists()
{
    State st;
    SetActionActive(st, true);
    StepState(st, HitSample(), DefaultCvars(), kDt, false, false, false);
    CHECK(st.latched);
    for (int i = 0; i < 30; ++i) {
        StepState(st, HitSample(1 + i, 2 + i), DefaultCvars(), kDt, false,
                  false, false);
    }
    CHECK(st.latched);
}

// 3. no hit -> the release path runs phase 3, which clears bit0/bit2 when
//    the timers are exhausted -- the exact decompiled behavior (the
//    audit's earlier "survives a miss" expectation predates the exact
//    phase-3 decompile; the mounted survival comes from the horse
//    sampler's persistence, not from the OnPress controller). A new
//    hold-E is required to re-engage.
void TestHitLossRelatch()
{
    State st;
    SetActionActive(st, true);
    StepState(st, HitSample(), DefaultCvars(), kDt, false, false, false);
    CHECK(st.latched);
    StepState(st, MissSample(), DefaultCvars(), kDt, false, false, false);
    CHECK(!st.latched);
    CHECK((st.flags & kBitActive) == 0); // the phase-3 teardown
    CHECK((st.flags & kBitSecond) == 0);
    // A later hit alone cannot relatch; a new hold-E engages again.
    StepState(st, HitSample(), DefaultCvars(), kDt, false, false, false);
    CHECK(!st.latched);
    SetActionActive(st, true);
    StepState(st, HitSample(), DefaultCvars(), kDt, false, false, false);
    CHECK(st.latched);
}

// 4. slot1 hit mirror toggles bit1 only.
void TestHitMirror()
{
    State st;
    SetActionActive(st, true);
    const unsigned char before = st.flags;
    StepState(st, HitSample(), DefaultCvars(), kDt, false, false, false);
    CHECK((st.flags & kBitHit) != 0);
    CHECK((st.flags & ~kBitHit) == (before & ~kBitHit));
    StepState(st, MissSample(), DefaultCvars(), kDt, false, false, false);
    CHECK((st.flags & kBitHit) == 0);
}

// 5. W-only manual ownership does not automatically destroy the controller
//    state (it arms the interruption; bit0 survives).
void TestManualDoesNotKillState()
{
    State st;
    SetActionActive(st, true);
    StepState(st, HitSample(), DefaultCvars(), kDt, false, false, false);
    StepState(st, HitSample(), DefaultCvars(), kDt, true, false, false);
    CHECK((st.flags & kBitArmed) != 0);
    CHECK((st.flags & kBitActive) != 0); // not destroyed yet
}

// 6. a brief interruption arms the state; once the input returns neutral,
//    the phase-0 clears the armed bit and -- after the DeactivateTime grace
//    elapses -- HintsActive is true again and the follow resumes on its
//    own (no new hold-E). The ReactivateTime window only runs while the
//    follow stays in the release path (sustained input -- test 7).
void TestBriefInterruptionResume()
{
    State st;
    SetActionActive(st, true);
    StepState(st, HitSample(), DefaultCvars(), kDt, false, false, false);
    CHECK(st.latched);
    // A couple of manual ticks arm the interruption and drop the latch.
    StepState(st, HitSample(), DefaultCvars(), kDt, true, false, false);
    StepState(st, HitSample(), DefaultCvars(), kDt, true, false, false);
    CHECK((st.flags & kBitArmed) != 0);
    CHECK(!st.latched);
    // The input returns neutral: the phase-0 clears the armed bit.
    StepState(st, HitSample(), DefaultCvars(), kDt, false, false, false);
    CHECK((st.flags & kBitArmed) == 0);
    // After the DeactivateTime grace the follow resumes by itself.
    const Cvars c = DefaultCvars();
    const int ticks = static_cast<int>(c.deactivateTime / kDt) + 10;
    for (int i = 0; i < ticks; ++i) {
        StepState(st, HitSample(), DefaultCvars(), kDt, false, false, false);
    }
    CHECK(st.latched);
    CHECK((st.flags & kBitActive) != 0);
}

// 7. sustained A/D -> deactivate/reactivate timers -> bit0/bit2 teardown.
void TestSustainedInterruptionDeactivates()
{
    State st;
    const Cvars c = DefaultCvars();
    SetActionActive(st, true);
    StepState(st, HitSample(), DefaultCvars(), kDt, false, false, false);
    // Hold manual input past the deactivate+reactivate window.
    const float window = c.deactivateTime + c.reactivateTime;
    const int ticks = static_cast<int>(window / kDt) + 20;
    for (int i = 0; i < ticks; ++i) {
        StepState(st, HitSample(), DefaultCvars(), kDt, true, false, false);
    }
    CHECK((st.flags & kBitActive) == 0);
    CHECK((st.flags & kBitSecond) == 0);
    CHECK(st.deactivateTime == 0.0f);
    CHECK(st.reactivateTime == 0.0f);
    CHECK(!st.latched);
    // After the teardown, a plain hit cannot relatch without a new action.
    StepState(st, HitSample(), DefaultCvars(), kDt, false, false, false);
    CHECK(!st.latched);
}

// 8. jump/chat path clears the exact native bits/timers.
void TestChatTeardown()
{
    State st;
    const Cvars c = DefaultCvars();
    SetActionActive(st, true);
    StepState(st, HitSample(), DefaultCvars(), kDt, false, false, false);
    st.flags |= kBitSecond; // emulate the second engage bit set
    st.deactivateTime = 0.3f;
    st.reactivateTime = 0.3f;
    StepState(st, HitSample(), DefaultCvars(), kDt, false, true, false);
    CHECK((st.flags & kBitActive) == 0);
    CHECK((st.flags & kBitSecond) == 0);
    CHECK(st.deactivateTime == 0.0f);
    CHECK(st.reactivateTime == 0.0f);
}

// 9. sample.failed -> phase5 exact bit/timer writes.
void TestPhase5Failure()
{
    State st;
    SetActionActive(st, true);
    StepState(st, HitSample(), DefaultCvars(), kDt, false, false, false);
    CHECK(st.latched);
    StepState(st, FailedSample(), DefaultCvars(), kDt, false, false, false);
    CHECK(!st.latched);
    CHECK((st.flags & kBitActive) == 0);
    CHECK((st.flags & kBitFlick) != 0);
    CHECK(st.deactivateTime == 0.0f);
    CHECK(st.reactivateTime == 0.0f);
}

// 10. pathB remains the rolling last-10 point ids with the native dedup
//     order.
void TestPathBRolling()
{
    State st;
    for (int i = 0; i < 20; ++i) {
        PushPathB(st, i, i + 1);
    }
    CHECK(st.pathBCount == State::kPathCapacity);
    // The rolling window holds the newest ids: ..., 18, 19, 20.
    CHECK(st.pathB[st.pathBCount - 1] == 20);
    CHECK(st.pathB[st.pathBCount - 2] == 19);
    // The native dedup: the same from-id again is skipped; consecutive
    // duplicates avoided.
    PushPathB(st, 20, 20);
    CHECK(st.pathBCount == State::kPathCapacity); // no growth
    CHECK(st.pathB[st.pathBCount - 1] == 20);     // no consecutive dup
}

// 11. the common publish writes live/hit/yaw from the sample.
void TestPublish()
{
    State st;
    SetActionActive(st, true);
    StepState(st, HitSample(7, 8, 1.25f), DefaultCvars(), kDt, false, false,
              false);
    CHECK(st.magnetismLive);
    CHECK(st.magnetYaw == 1.25f);
    CHECK(st.magnetHitX == 10.0f);
    CHECK(st.magnetHitY == 20.0f);
    CHECK(st.magnetHitZ == 1.0f);
}

// 12. repeated ticks never produce contradictory latch/flag combos beyond
//     the native transient states (the chat/jump teardown clears bit0
//     after the success path has already set the latch for the tick).
void TestNoContradictions()
{
    State st;
    SetActionActive(st, true);
    for (int i = 0; i < 300; ++i) {
        SampleInput s;
        switch (i % 5) {
        case 0:
        case 1:
        case 2:
            s = HitSample(1 + i, 2 + i);
            break;
        case 3:
            s = MissSample();
            break;
        default:
            s = FailedSample();
            break;
        }
        const bool manual = (i % 13) == 0 || (i % 17) == 0;
        const bool chat = i % 31 == 0;
        StepState(st, s, DefaultCvars(), kDt, manual, chat, false);
        // The latch only ever holds while the last tick had a hit AND the
        // controller was eligible (bit0 + timers + not armed) at slot 1.
        if (st.latched) {
            CHECK(s.hasHit);
            // The chat teardown is the one native transient that can leave
            // the latch set while bit0 is cleared at the tick's end.
            if (!chat) {
                CHECK((st.flags & kBitActive) != 0);
            }
        }
        if (st.magnetismLive) {
            CHECK(st.latched);
        }
    }
}

// 13. an activation completed during manual movement is consumed and rejected,
//     never retained until movement later becomes neutral.
void TestManualActivationIsNotDeferred()
{
    ToggleRequest request;
    request.Unblock(ToggleBlocker::Context);
    request.Request();
    CHECK(request.IsPending());
    CHECK(request.Consume(false, true, true) == ToggleResolution::Cancelled);
    CHECK(!request.IsPending());
    CHECK(request.Consume(false, false, true) == ToggleResolution::None);
}

// 14. menu/context cancellation and invalid action state both discard the
//     completed hold without producing a later operation.
void TestToggleCancellation()
{
    ToggleRequest request;
    request.Unblock(ToggleBlocker::Context);
    request.Request();
    request.Cancel();
    CHECK(request.Consume(false, false, true) == ToggleResolution::None);

    request.Request();
    CHECK(request.Consume(false, false, false) == ToggleResolution::Cancelled);
    CHECK(!request.IsPending());

    request.Block(ToggleBlocker::RootMenu);
    request.Request();
    request.Unblock(ToggleBlocker::RootMenu);
    CHECK(request.Consume(false, false, true) == ToggleResolution::None);

    request.Request();
    request.Block(ToggleBlocker::RootMenu);
    request.Request();
    request.Unblock(ToggleBlocker::RootMenu);
    CHECK(request.Consume(false, false, true) == ToggleResolution::None);

    request.Block(ToggleBlocker::RootMenu);
    request.Block(ToggleBlocker::FullUI);
    request.Unblock(ToggleBlocker::RootMenu);
    request.Request();
    CHECK(request.IsBlocked());
    CHECK(request.Consume(false, false, true) == ToggleResolution::None);
    request.Unblock(ToggleBlocker::FullUI);
    CHECK(!request.IsBlocked());
}

// 15. callbacks from both same-key rows collapse into one operation.
void TestDuplicateToggleCallbacksCollapse()
{
    ToggleRequest request;
    request.Unblock(ToggleBlocker::Context);
    request.Request();
    request.Request();
    CHECK(request.Consume(false, false, true) == ToggleResolution::Engage);
    CHECK(request.Consume(false, false, true) == ToggleResolution::None);
}

// 16. a valid request engages the road machine on a hit.
void TestToggleEngagesMachine()
{
    State st;
    ToggleRequest request;
    request.Unblock(ToggleBlocker::Context);
    request.Request();
    const auto result = request.Consume(st.latched, false, true);
    CHECK(result == ToggleResolution::Engage);
    if (result == ToggleResolution::Engage) {
        SetActionActive(st, true);
    }
    StepState(st, HitSample(), DefaultCvars(), kDt, false, false, false);
    CHECK(st.latched);
    CHECK((st.flags & kBitActive) != 0);
}

// 17. a contextual toggle while latched remains a disengagement even if
//     manual movement is currently authoritative.
void TestToggleDisengagesMachine()
{
    State st;
    SetActionActive(st, true);
    StepState(st, HitSample(), DefaultCvars(), kDt, false, false, false);
    CHECK(st.latched);

    ToggleRequest request;
    request.Unblock(ToggleBlocker::Context);
    request.Request();
    const auto result = request.Consume(st.latched, true, true);
    CHECK(result == ToggleResolution::Disengage);
    if (result == ToggleResolution::Disengage) {
        SetActionActive(st, false);
    }
    StepState(st, HitSample(), DefaultCvars(), kDt, false, false, false);
    CHECK(!st.latched);
    CHECK((st.flags & kBitActive) == 0);
}

// 18. physical W takes ownership of the shared engine symbol before the
//     plugin releases its synthetic hold.
void TestManualWOwnershipResolution()
{
    using AutoWalk::FollowInteraction::ResolveManualW;

    const auto syntheticOnly = ResolveManualW(true, true, false);
    CHECK(!syntheticOnly.manualHeld);
    CHECK(syntheticOnly.releaseSynthetic);

    const auto physicalDuringSynthetic = ResolveManualW(true, true, true);
    CHECK(physicalDuringSynthetic.manualHeld);
    CHECK(!physicalDuringSynthetic.releaseSynthetic);

    const auto transferredToPhysical = ResolveManualW(false, true, true);
    CHECK(transferredToPhysical.manualHeld);
    CHECK(!transferredToPhysical.releaseSynthetic);

    const auto idle = ResolveManualW(false, false, false);
    CHECK(!idle.manualHeld);
    CHECK(!idle.releaseSynthetic);
}

} // namespace

int main()
{
    TestEngage();
    TestFollowPersists();
    TestHitLossRelatch();
    TestHitMirror();
    TestManualDoesNotKillState();
    TestBriefInterruptionResume();
    TestSustainedInterruptionDeactivates();
    TestChatTeardown();
    TestPhase5Failure();
    TestPathBRolling();
    TestPublish();
    TestNoContradictions();
    TestManualActivationIsNotDeferred();
    TestToggleCancellation();
    TestDuplicateToggleCallbacksCollapse();
    TestToggleEngagesMachine();
    TestToggleDisengagesMachine();
    TestManualWOwnershipResolution();

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
