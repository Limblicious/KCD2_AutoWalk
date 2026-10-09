#pragma once

namespace wh::entitymodule {
class C_Player;
}

namespace AutoWalk::HorseCameraRecenter {

// Clears the mounted-style blend-in state. Call whenever autonomous movement
// stops or another camera owner takes control. The look-activity stamp is
// owned by the input events (NotifyMouseLook) and is not refreshed here: the
// native reset helper runs on look-input frames, and the other reset paths
// (UI, disengage) have no native stamping analog.
void Reset();

// Sets whether look-input monitoring is registered. The recenter is
// fail-closed: while monitoring is false the view pull never runs, so an
// unmonitored camera can never be recentered without a way to interrupt it.
void SetLookMonitoring(bool active);

// Records user look activity (a look-axis input event). The native mounted
// routine resets its centering state every frame the look vector is
// nonzero; the on-foot equivalent is the look-axis event stream.
void NotifyMouseLook();

// Port of the applicable part of MountedCamera_Centering (REL 56442). The
// lookPitch/lookYaw values are the movement request's computed look terms
// (diagnostics only): the actual interruption gate is the look activity
// recorded by NotifyMouseLook plus the native CameraCenteringTime delay.
// bodyYaw/moveYaw/cameraYaw are ground-truth diagnostics (radians);
// moveValid says whether moveYaw was actually measured.
void Update(wh::entitymodule::C_Player* player, float travelYaw, float dt,
            float lookPitch, float lookYaw,
            float bodyYaw, float moveYaw, bool moveValid, float cameraYaw);

} // namespace AutoWalk::HorseCameraRecenter
