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
// on the game's contextual action system (I_ActionSets) and toggles the
// helpbar rows (activate while idle on a road, deactivate while following).
void RegisterPromptActions();
enum class PromptState { Hidden, Activate, Deactivate };
void SetPromptState(PromptState state);

// Engagement callbacks wired to the native hold-E dispatch.
void RequestEngage();
void RequestDisengage();

// One controller step; called from the plugin's recurring KCSE task.
void Tick();

// Steering sign tuning (empirical convention mapping; default 1).
extern int g_steerInvert;

} // namespace AutoWalk::FollowController
