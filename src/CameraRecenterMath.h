#pragma once

// Pure target/delta construction of the mounted-style camera recenter,
// shared between the runtime module and the deterministic tests.
// Requires CryEngine math types only (Cry_Math.h).

#include <cmath>

#include "CryEngine/CryCommon/Cry_Math.h"

namespace AutoWalk::HorseCameraRecenter {

// Wrap an angle into (-pi, pi].
inline float WrapPi(float a)
{
    while (a > 3.14159265f) a -= 6.28318531f;
    while (a < -3.14159265f) a += 6.28318531f;
    return a;
}

// Builds the autonomous recenter target quaternion. The road-follow command
// (travelYaw) is flat-relative: the native sampler returns a
// facing-normalized road direction and the movement consumer applies the
// request in the flat-yaw frame, so the true world movement heading is
// flatYaw + travelYaw (runtime-verified: prediction B across three
// conditions, error within ~2 degrees). The CameraCenteringPitchOffset
// (radians) is the native pitch term.
inline Quat RecenterTarget(float flatYaw, float travelYaw, float pitchOffset)
{
    return Quat::CreateRotationZ(WrapPi(flatYaw + travelYaw)) *
           Quat::CreateRotationX(pitchOffset);
}

// REL 56442: inverse(currentView) * slerp(currentView, target, blend),
// converted to Euler, then added to the actor look accumulator.
inline Ang3 RecenterDelta(const Quat& current, const Quat& desired,
                          float blend)
{
    Quat target = desired;
    if (blend < 1.0f) {
        target.SetSlerp(current, desired, blend);
    }
    return Ang3((!current) * target);
}

// Caps the per-frame pull magnitude. The native centering never applies the
// full remaining error in one frame (the blend stage limits the pull to the
// CameraCentering rate, and the heading stage is critically damped); on foot
// the composed target can retreat with the view, so an uncapped full-error
// pull stacks and spins. Clamping to the native CameraCentering rate keeps
// the pull below the road-follow compensation rate.
inline Ang3 ClampDeltaStep(const Ang3& delta, float maxStep)
{
    const float mag = std::sqrt(delta.x * delta.x + delta.y * delta.y +
                                delta.z * delta.z);
    if (mag > maxStep && mag > 0.0f) {
        return delta * (maxStep / mag);
    }
    return delta;
}

// Dead-zone on the remaining error. On foot the flat yaw follows the view,
// so a small camera bobble propagates through the sampler's facing
// normalization and the travel smoother (with its native sign-flip reset)
// back into the target -- a self-sustained limit cycle of a couple of
// degrees (runtime-observed). Below the zone the pull is skipped so the
// coupled loop settles. The native needs no zone because its target is
// view-independent.
inline bool InsideDeadZone(const Ang3& error, float zone)
{
    return std::abs(error.x) < zone && std::abs(error.y) < zone &&
           std::abs(error.z) < zone;
}

// Proportional pull velocity toward the target: fast initial swing,
// decelerating approach (exponential), continuous curve tracking, and a
// dead-zone whose boundary velocity is continuous (the zone is subtracted
// from the error, so the velocity approaches zero at the boundary instead
// of jumping). gain must stay below the road-follow smoother's rate so its
// compensation can keep up with the pull (runtime-established stability
// constraint).
inline float PullVelocity(float error, float gain, float maxRate,
                          float deadZone)
{
    const float magnitude = std::abs(error) - deadZone;
    if (magnitude <= 0.0f) {
        return 0.0f;
    }
    const float v = (error < 0.0f ? -magnitude : magnitude) * gain;
    if (v > maxRate) {
        return maxRate;
    }
    if (v < -maxRate) {
        return -maxRate;
    }
    return v;
}

} // namespace AutoWalk::HorseCameraRecenter
