#pragma once

#include <cstdint>
#include <string>

#include "CryEngine/CryCommon/SInputEvent.h"

namespace wh::entitymodule { class C_Player; }

namespace AutoWalk::InputSeam {

// Resolves the interned action-name object for an action that is currently
// registered by the game, then feeds it through the player vtable slot 148
// simulated-action queue (the exact seam SimulateOnAction uses).
//
// Fails closed: returns false without touching the game when the player,
// the name, or the registration lookup is unavailable.
bool SimulateAction(wh::entitymodule::C_Player* player,
                    const char* name,
                    int mode,
                    float value);

// True when 'name' is currently one of the six registered action slots.
bool IsActionRegistered(const char* name);

// Read-only dump of the six registered-action slots and the manager object.
std::string DescribeRegisteredActions();

// Posts a synthesized key event through the engine input pipeline
// (IInput::PostInputEvent) -- the same path real key input takes, so the
// action map, blocking, and camera/facing coupling all behave normally.
bool PostKeyEvent(Offsets::EKeyId key, Offsets::EInputState state, float value);

// True when the key's symbol is currently in the engine held-key queue.
bool IsKeyHeld(Offsets::EKeyId key);

// Posts a synthesized mouse X delta (eIS_Changed) -- equivalent to moving the
// mouse horizontally; drives vanilla on-foot turning via the camera.
bool PostMouseDelta(float dx);

// Read-only pipeline diagnostic: symbol lookup, posting flag, listener count,
// held-queue contents; then performs ONE test press and reports the symbol
// state/queue afterwards. Console-gated diagnostic.
std::string DescribeInputPipeline();

} // namespace AutoWalk::InputSeam
