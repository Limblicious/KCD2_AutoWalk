#pragma once

namespace AutoWalk::PlayerMovement {

bool IsPlayerMounted();

// Future seam:
// 1. sustain ordinary on-foot forward movement, equivalent in intent to held W;
// 2. apply native road-follow heading/turn correction through an appropriate
//    on-foot steering seam.
//
// Leave KCD2's normal on-foot camera/facing coupling untouched.
// Do not synthesize a camera-relative strafe vector and do not directly
// rotate, lock, or decouple the camera.

} // namespace AutoWalk::PlayerMovement
