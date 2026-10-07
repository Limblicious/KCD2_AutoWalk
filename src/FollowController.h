#pragma once

namespace AutoWalk::FollowController {

enum class Phase {
    Disabled,
    AwaitingRoad,
    Following
};

Phase GetPhase();

// Enables on-foot road following (console override / diagnostic).
void Enable();
void Disable();
void Reset();

// Native hold-E prompt integration: registers the two foot-magnetism actions
// on the game's contextual action system (I_ActionSets) and drives the
// helpbar rows exactly like the mounted prompt updater (disable reason +
// enabled + visible per row).
void RegisterPromptActions();
void UpdatePromptFlags(bool onRoad, bool engaged, bool manualHeld);

// Engagement callbacks wired to the native hold-E dispatch.
void RequestEngage();
void RequestDisengage();

// One controller step; called from the plugin's recurring KCSE task.
void Tick();

// Steering sign tuning (empirical convention mapping; default 1).
extern int g_steerInvert;

} // namespace AutoWalk::FollowController
