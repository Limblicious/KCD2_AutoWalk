#pragma once

namespace wh::entitymodule {
class C_Player;
}

namespace AutoWalk::HorseCameraRecenter {

// Reference-frame ground truth gathered each follow frame by the movement
// hook and passed to Update for diagnostics. All yaws are world-space
// headings in radians (CryEngine convention: forward = (-sin z, cos z)).
// Diagnostics only: nothing here feeds the camera or movement math.
struct GroundTruth {
    float travelYaw = 0.0f;        // autonomous travel heading (the recenter target)
    float requestYaw = 0.0f;       // S_MountAnimState::m_desiredVelocity direction AFTER the override
    float vanillaRequestYaw = 0.0f; // ...BEFORE the override (vanilla controller output)
    float flatYaw = 0.0f;          // C_ActorPhysicsState::m_flatYaw (+0x44)
    float entityYaw = 0.0f;        // entity world-TM forward heading
    float cameraYaw = 0.0f;        // rendered camera heading (CSystem view camera)
    float moveYaw = 0.0f;          // consecutive-frame displacement heading
    bool  moveValid = false;
    float windowYaw = 0.0f;        // rolling ~0.5 s displacement heading
    bool  windowValid = false;
    float dx = 0.0f;               // raw per-frame world delta (meters)
    float dy = 0.0f;
    float dt = 0.0f;               // frame time (seconds)
};

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
// gt supplies the ground-truth values for the diagnostic log only.
void Update(wh::entitymodule::C_Player* player, float dt,
            float lookPitch, float lookYaw, const GroundTruth& gt);

} // namespace AutoWalk::HorseCameraRecenter
