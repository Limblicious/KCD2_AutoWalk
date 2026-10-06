#pragma once

namespace AutoWalk::PlayerMovement {

bool IsPlayerMounted();

// Future seam: inject a normalized camera-relative movement vector after
// on-foot physical input is resolved but before locomotion consumes it.
// Do not rotate the camera and do not directly set player speed.

} // namespace AutoWalk::PlayerMovement
