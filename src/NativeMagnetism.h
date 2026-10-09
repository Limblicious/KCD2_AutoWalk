#pragma once

namespace AutoWalk::NativeMagnetism {

// Tuning read live from the native frame-helper object (REL 38017) at the
// recovered S_HorseCVars offsets (see docs/REVERSE_ENGINEERING.md).
struct FrameCVars {
    float enterAngle = 0.0f;     // +0x84  RoadMagnetismEnterAngle
    float remainAngle = 0.0f;    // +0x88  RoadMagnetismRemainAngle
    float snapTime = 0.0f;       // +0x98  RoadMagnetismSnapTime
    float rotationMax = 0.0f;    // +0xE8  RotationMax
    float smoothOutSpeed = 0.0f; // +0xEC  RotationSmoothOutSpeed
    float smoothInSpeed = 0.0f;  // +0xF0  RotationSmoothInSpeed
    float cameraCentering = 0.0f; // +0x100 CameraCentering
    float cameraCenteringTime = 0.0f; // +0x104 CameraCenteringTime
    float cameraCenteringPitchOffset = 0.0f; // +0x110 CameraCenteringPitchOffset
    float clampDelta = 0.0f;     // +0x16C ClampDelta
    float roadDistOff = 0.0f;    // +0xD4  RoadMagnetismOnPressRoadDistOff
    float roadDistOn = 0.0f;     // +0xD8  RoadMagnetismOnPressRoadDistOn
    float deactivateTime = 0.0f; // +0x204 RoadMagnetismDeactivateTime
    float reactivateTime = 0.0f; // +0x208 RoadMagnetismReactivateTime
};

// Refreshes the cached cvars from the native frame helper. Fail-closed:
// returns false and leaves the previous values when the helper is absent.
bool RefreshFrameCVars(const FrameCVars*& out);

// Port of HorseYaw_SmoothCD (REL 31821). Target is the road yaw command;
// smoothing constants recovered from the binary (see REVERSE_ENGINEERING.md).
struct YawSmoother {
    float smoothed = 0.0f;
    float vel = 0.0f;
    bool initialized = false;
};

void SmoothCD(YawSmoother& s, float target, float dt, const FrameCVars& cvars);

// Port of the S_OnPressController state machine (see REVERSE_ENGINEERING.md,
// Phase C). bits: 0x01 active, 0x02 latched, 0x04 reserved, 0x08 flick,
// 0x10 armed (interruption timers running).
struct OnPressState {
    unsigned char flags = 0;
    float deactivateTime = 0.0f;
    float reactivateTime = 0.0f;
    float hintTime = 0.0f;
};

// Advance the recovered timer/state transitions. Returns false when the
// follow must deactivate (all bits cleared).
bool TickStateMachine(OnPressState& st, float dt, const FrameCVars& cvars,
                      bool manualInputActive, bool chatFollowActive,
                      bool sampleFailed, bool engageRequested);

constexpr float kRadToDeg = 57.295779513f;

} // namespace AutoWalk::NativeMagnetism
