// Deterministic tests for the mounted-style camera recenter: the pure
// timing state machine (CameraRecenterTiming.h) and the target/delta
// construction (CameraRecenterMath.h). No game dependencies.

#include "../src/CameraRecenterMath.h"
#include "../src/CameraRecenterTiming.h"

#include <cmath>
#include <cstdio>

using namespace AutoWalk::HorseCameraRecenter;

namespace {

constexpr float kDt = 1.0f / 60.0f;
constexpr float kDegToRad = 0.01745329252f;

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

// --- Timing state machine -------------------------------------------------

// 1. The native CameraCenteringTime restart delay holds the pull after the
//    last look input.
void TestDelayHolds()
{
    float blend = 0.0f;
    CHECK(!StepTiming(0.2f, 3.0f, 1.0f, kDt, blend));
    CHECK(blend == 0.0f);
    CHECK(!StepTiming(0.2f, 3.0f, 2.99f, kDt, blend));
    CHECK(blend == 0.0f);
}

// 2. Once the delay elapses the pull starts and the blend accumulates at
//    the CameraCentering rate.
void TestDelayElapsedStartsBlend()
{
    float blend = 0.0f;
    CHECK(StepTiming(0.2f, 3.0f, 3.0f, kDt, blend));
    CHECK(blend > 0.0f);
    CHECK(blend < 0.01f);
}

// 3. The blend clamps at 1 and stays there.
void TestBlendClamps()
{
    float blend = 0.0f;
    for (int i = 0; i < 60 * 30; ++i) {
        StepTiming(0.2f, 3.0f, 100.0f, kDt, blend);
    }
    CHECK(blend == 1.0f);
}

// 4. A look event inside the window resets the blend to zero again.
void TestDelayRestarts()
{
    float blend = 1.0f;
    CHECK(!StepTiming(0.2f, 3.0f, 0.5f, kDt, blend));
    CHECK(blend == 0.0f);
}

// 5. A disabled centering delay (<= 0) applies immediately.
void TestNoDelay()
{
    float blend = 0.0f;
    CHECK(StepTiming(0.2f, 0.0f, 0.0f, kDt, blend));
    CHECK(StepTiming(0.2f, -1.0f, 0.0f, kDt, blend));
}

// --- Target / delta construction ------------------------------------------

// 6. With a full blend, the delta pulls the identity view exactly onto the
//    composed world target yaw: the target construction maps
//    WrapPi(flatYaw + travelYaw) to view yaw with no offset or sign flip.
void TestDeltaYawConvention()
{
    const Quat current = Quat::CreateIdentity();
    const Quat desired = RecenterTarget(0.0f, 0.5f, 0.0f);
    const Ang3 delta = RecenterDelta(current, desired, 1.0f);
    CHECK(std::abs(delta.z - 0.5f) < 1.0e-4f);
    CHECK(std::abs(delta.x) < 1.0e-4f);
}

// 7. Zero blend produces no pull.
void TestDeltaZeroBlend()
{
    const Quat current = Quat::CreateIdentity();
    const Quat desired = RecenterTarget(0.0f, 1.5f, 0.0f);
    const Ang3 delta = RecenterDelta(current, desired, 0.0f);
    CHECK(std::abs(delta.z) < 1.0e-4f);
    CHECK(std::abs(delta.x) < 1.0e-4f);
}

// 8. The native pitch offset is applied on the pitch axis only.
void TestDeltaPitchOffset()
{
    const float pitch = -2.5f * kDegToRad;
    const Quat desired = RecenterTarget(0.0f, 0.0f, pitch);
    const Ang3 delta = RecenterDelta(Quat::CreateIdentity(), desired, 1.0f);
    CHECK(std::abs(delta.x - pitch) < 1.0e-4f);
    CHECK(std::abs(delta.z) < 1.0e-4f);
}

// 9. The delta is frame-relative: pulling from an already-rotated view
//    toward the same target shrinks with the remaining angle.
void TestDeltaFromRotatedView()
{
    const Quat current = Quat::CreateRotationZ(0.4f);
    const Quat desired = RecenterTarget(0.0f, 0.5f, 0.0f);
    const Ang3 delta = RecenterDelta(current, desired, 1.0f);
    CHECK(std::abs(delta.z - 0.1f) < 1.0e-4f);
}

// --- Reference-frame composition (runtime-verified samples) ----------------

// 10. The composed world target reproduces the measured displacement heading
//     from the three-condition runtime logs (prediction B: move =
//     WrapPi(flat + request)). Samples: 1529, 1564, 1620. Tolerance 0.005 rad
//     (~0.29 deg) covers the log's ~0.2 deg sampling-lag residual.
void TestTargetCompositionFromRuntimeSamples()
{
    // Line 1529 (locked recenter): flat=-1.307489, travel=-1.307491,
    // measured moveWin=-2.614908.
    {
        const Quat q = RecenterTarget(-1.307489f, -1.307491f, 0.0f);
        const Ang3 a(q);
        CHECK(std::abs(WrapPi(-1.307489f + -1.307491f) - a.z) < 1.0e-4f);
        CHECK(std::abs(WrapPi(a.z - (-2.614908f))) < 0.005f);
    }
    // Line 1564 (view held ~90 deg right): flat=2.690169, travel=1.187411,
    // measured moveWin=-2.404747.
    {
        const Quat q = RecenterTarget(2.690169f, 1.187411f, 0.0f);
        const Ang3 a(q);
        CHECK(std::abs(WrapPi(a.z - (-2.404747f))) < 0.005f);
    }
    // Line 1620 (looking down the path): flat=-2.648023, travel=0.058675,
    // measured moveWin=-2.592822.
    {
        const Quat q = RecenterTarget(-2.648023f, 0.058675f, 0.0f);
        const Ang3 a(q);
        CHECK(std::abs(WrapPi(a.z - (-2.592822f))) < 0.005f);
    }
}

// 11. WrapPi normalizes both directions.
void TestWrapPi()
{
    CHECK(std::abs(WrapPi(3.877580f) - (-2.405605f)) < 1.0e-4f);
    CHECK(std::abs(WrapPi(-2.5f) + 2.5f) < 1.0e-6f);
}

// 12. The per-frame pull is capped at the CameraCentering rate: an
//     uncapped full-error pull into a retreating target stacked into a spin
//     (runtime-observed). The clamp must bound the step and preserve its
//     direction, and pass small deltas through unchanged.
void TestDeltaStepClamp()
{
    // Large delta: capped to maxStep, direction preserved.
    {
        const Ang3 delta(0.6f, 0.0f, 0.0f);
        const Ang3 clamped = ClampDeltaStep(delta, 0.01f);
        CHECK(std::abs(clamped.x - 0.01f) < 1.0e-6f);
        CHECK(std::abs(clamped.y) < 1.0e-6f);
        CHECK(std::abs(clamped.z) < 1.0e-6f);
    }
    // Small delta: unchanged.
    {
        const Ang3 delta(0.005f, 0.0f, 0.0f);
        const Ang3 clamped = ClampDeltaStep(delta, 0.01f);
        CHECK(std::abs(clamped.x - 0.005f) < 1.0e-6f);
    }
    // The native rate: CameraCentering * dt at 60 fps.
    {
        const float centering = 0.2f;
        const float dt = 1.0f / 60.0f;
        const Ang3 delta(1.5f, 0.0f, 0.0f);
        const Ang3 clamped = ClampDeltaStep(delta, centering * dt);
        CHECK(std::abs(clamped.x - centering * dt) < 1.0e-6f);
    }
}

// 13. The coupled-frame limit cycle is suppressed by the dead-zone: errors
//     below the zone produce no pull, errors above do.
void TestDeadZone()
{
    constexpr float kDegToRad = 0.01745329252f;
    const float zone = 2.0f * kDegToRad;
    CHECK(InsideDeadZone(Ang3(0.5f * kDegToRad, 0.0f, 0.0f), zone));
    CHECK(InsideDeadZone(Ang3(0.0f, 0.0f, 1.9f * kDegToRad), zone));
    CHECK(!InsideDeadZone(Ang3(0.0f, 0.0f, 2.1f * kDegToRad), zone));
    CHECK(!InsideDeadZone(Ang3(2.1f * kDegToRad, 0.0f, 0.0f), zone));
}

} // namespace

int main()
{
    TestDelayHolds();
    TestDelayElapsedStartsBlend();
    TestBlendClamps();
    TestDelayRestarts();
    TestNoDelay();
    TestDeltaYawConvention();
    TestDeltaZeroBlend();
    TestDeltaPitchOffset();
    TestDeltaFromRotatedView();
    TestTargetCompositionFromRuntimeSamples();
    TestWrapPi();
    TestDeltaStepClamp();
    TestDeadZone();

    std::printf("CameraRecenterTests: %d checks, %d failures\n", g_checks,
                g_failures);
    return g_failures == 0 ? 0 : 1;
}
