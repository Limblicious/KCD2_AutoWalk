#pragma once

namespace AutoWalk::FollowController {

enum class Phase {
    Disabled,
    Following,
    AwaitingRoad
};

Phase GetPhase();

// Enables on-foot road following. Fail-closed: movement is only ever held
// while a valid road sample exists and the player is on foot.
void Enable();

// Stops following and releases any held forward key.
void Disable();

// Stops following without logging (load/new-game reset path).
void Reset();

// One controller step; called from the plugin's recurring KCSE task.
void Tick();

// Steering tunables (registered as cvars from plugin.cpp).
extern float g_steerGain;      // mouse delta per radian of smoothed error
extern float g_steerMax;       // max mouse delta per frame
extern float g_steerLookahead; // aim-ahead fraction of the segment
extern int g_steerInvert;      // 1 to flip steering direction

} // namespace AutoWalk::FollowController
