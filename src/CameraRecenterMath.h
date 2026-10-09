#pragma once

// Pure target/delta construction of the mounted-style camera recenter,
// shared between the runtime module and the deterministic tests.
// Requires CryEngine math types only (Cry_Math.h).

#include "CryEngine/CryCommon/Cry_Math.h"

namespace AutoWalk::HorseCameraRecenter {

// Builds the autonomous recenter target quaternion. The native mounted
// target is horseEntityRotation * relativeYaw * pitch(...); on foot the
// autonomous travel frame replaces the horse entity orientation. The
// CameraCenteringPitchOffset (radians) is the native pitch term.
inline Quat RecenterTarget(float travelYaw, float pitchOffset)
{
    return Quat::CreateRotationZ(travelYaw) *
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

} // namespace AutoWalk::HorseCameraRecenter
