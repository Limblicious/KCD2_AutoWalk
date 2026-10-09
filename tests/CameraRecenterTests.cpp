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
//    travel yaw: the target construction maps travelYaw to view yaw with
//    no offset or sign flip.
void TestDeltaYawConvention()
{
    const Quat current = Quat::CreateIdentity();
    const Quat desired = RecenterTarget(0.5f, 0.0f);
    const Ang3 delta = RecenterDelta(current, desired, 1.0f);
    CHECK(std::abs(delta.z - 0.5f) < 1.0e-4f);
    CHECK(std::abs(delta.x) < 1.0e-4f);
}

// 7. Zero blend produces no pull.
void TestDeltaZeroBlend()
{
    const Quat current = Quat::CreateIdentity();
    const Quat desired = RecenterTarget(1.5f, 0.0f);
    const Ang3 delta = RecenterDelta(current, desired, 0.0f);
    CHECK(std::abs(delta.z) < 1.0e-4f);
    CHECK(std::abs(delta.x) < 1.0e-4f);
}

// 8. The native pitch offset is applied on the pitch axis only.
void TestDeltaPitchOffset()
{
    const float pitch = -2.5f * kDegToRad;
    const Quat desired = RecenterTarget(0.0f, pitch);
    const Ang3 delta = RecenterDelta(Quat::CreateIdentity(), desired, 1.0f);
    CHECK(std::abs(delta.x - pitch) < 1.0e-4f);
    CHECK(std::abs(delta.z) < 1.0e-4f);
}

// 9. The delta is frame-relative: pulling from an already-rotated view
//    toward the same target shrinks with the remaining angle.
void TestDeltaFromRotatedView()
{
    const Quat current = Quat::CreateRotationZ(0.4f);
    const Quat desired = RecenterTarget(0.5f, 0.0f);
    const Ang3 delta = RecenterDelta(current, desired, 1.0f);
    CHECK(std::abs(delta.z - 0.1f) < 1.0e-4f);
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

    std::printf("CameraRecenterTests: %d checks, %d failures\n", g_checks,
                g_failures);
    return g_failures == 0 ? 0 : 1;
}
