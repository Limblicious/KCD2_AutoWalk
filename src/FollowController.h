#pragma once

namespace AutoWalk::FollowController {

enum class Phase {
    Disabled,
    AwaitingRoad,
    Following
};

Phase GetPhase();

// Enables on-foot road following (E-hold engage or console override).
void Enable();
void Disable();
void Reset();

// One controller step; called from the plugin's recurring KCSE task.
void Tick();

// Steering sign tuning (empirical convention mapping; default 1).
extern int g_steerInvert;

} // namespace AutoWalk::FollowController
