#pragma once

namespace wh::entitymodule {
class C_Player;
}

namespace AutoWalk::HorseCameraRecenter {

// Clears the mounted-style blend-in state. Call whenever autonomous movement
// stops or another camera owner takes control.
void Reset();

// Records user look activity (a mouse axis event). The native mounted
// routine resets its centering state every frame the look vector is
// nonzero; the on-foot equivalent is the raw mouse axis input.
void NotifyMouseLook();

// Port of the applicable part of MountedCamera_Centering (REL 56442). The
// lookPitch/lookYaw values are the movement request's computed look terms
// (diagnostics only): the actual interruption gate is the look activity
// recorded by NotifyMouseLook plus the native CameraCenteringTime delay.
// bodyYaw/moveYaw/cameraYaw are ground-truth diagnostics (radians).
void Update(wh::entitymodule::C_Player* player, float travelYaw, float dt,
            float lookPitch, float lookYaw,
            float bodyYaw, float moveYaw, float cameraYaw);

} // namespace AutoWalk::HorseCameraRecenter
