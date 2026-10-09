#pragma once

// Pure, game-independent timing logic of the mounted-style camera recenter
// (MountedCamera_Centering, REL 56442). Shared between the runtime module and
// the deterministic tests so the tested code is exactly the shipped logic.
//
// Native flow: every frame with look input refreshes the native reset stamp;
// while now - stamp < CameraCenteringTime the centering holds; afterwards the
// blend accumulates at the CameraCentering rate until it reaches 1.

namespace AutoWalk::HorseCameraRecenter {

// Advances the recenter blend state for one frame.
//   centering        native CameraCentering rate (per second, >= 0)
//   centeringTimeSec native CameraCenteringTime (seconds; <= 0 disables the
//                    restart delay)
//   sinceLookSec     seconds since the last recorded user look input
//   dt               frame time (seconds)
//   blend            in/out cumulative blend in [0, 1]
// Returns true when the view pull may run this frame (delay elapsed);
// returns false and resets the blend while the delay holds.
inline bool StepTiming(float centering, float centeringTimeSec,
                       float sinceLookSec, float dt, float& blend)
{
    if (centeringTimeSec > 0.0f && sinceLookSec < centeringTimeSec) {
        blend = 0.0f;
        return false;
    }
    blend += centering * dt;
    if (blend > 1.0f) {
        blend = 1.0f;
    }
    return true;
}

} // namespace AutoWalk::HorseCameraRecenter
