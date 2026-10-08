#pragma once

#include "NativeMagnetism.h"
#include "RoadFollowMachine.h"

namespace AutoWalk::FootRoad {
struct FootRoadProbe;
}

namespace AutoWalk::RoadFollowPort {

// Game-facing wrapper around the pure mode-1 machine (RoadFollowMachine.h):
// samples the road through the proven native wrapper (FootRoad, REL 194146)
// with the native acquisition radius (GetRoadDistance -> the OnPress
// RoadMagnetismOnPressRoadDistOff/On cvars) and feeds the recovered
// state machine. UNWIRED: not yet driven by FollowController.
//
// Movement ownership and controller interruption stay separate: the manual
// input only feeds the OnPress phases; the movement hook's ownership switch
// is FollowController's concern (Track A), not this module's.

// Engage/disengage via the hold-E action: bit0 = the action-controlled
// active state.
void SetActionActive(bool active);

// One recovered tick. Runs the sampler + the machine step.
void Tick(float dt, const NativeMagnetism::FrameCVars& cvars,
          bool manualInputHeld, bool chatFollow,
          FootRoad::FootRoadProbe& outSample);

// The current machine state.
const State& GetState();

} // namespace AutoWalk::RoadFollowPort
