#pragma once

namespace AutoWalk::RoadFollowPort {

// Pure mode-1 (S_OnPressController) road-follow state machine, ported
// branch-for-branch from the recovered native flow (docs/REVERSE_ENGINEERING.md):
//   S_HorseRoadFollow::Tick (0x180A4E5AC)
//   S_OnPressController_Tick phases (0x180A4E768)
//   OnPress slot-1 (0x180A4E98C): bit1 = sample hit; returns HintsActive
//   HintsActive (0x180A4E99C): bit0 && !(deactivateTime>0) && !bit4
//   PushPathB (0x180A4E39C) / PublishMagnetism (0x180A4DE5C)
//
// This translation unit is game-independent (no WHGame/KCSE includes) so the
// deterministic state-machine tests can link it standalone. The game-facing
// sampling/wiring lives in RoadFollowPort.

// Native cvars consumed by the mode-1 controller (the frame-helper offsets).
struct Cvars {
    float roadDistOff = 0.0f;    // +0xD4 RoadMagnetismOnPressRoadDistOff
    float roadDistOn = 0.0f;     // +0xD8 RoadMagnetismOnPressRoadDistOn
    float remainAngle = 0.0f;    // +0x88 RoadMagnetismRemainAngle (the phase-2 stay threshold)
    float deactivateTime = 0.0f; // +0x204
    float reactivateTime = 0.0f; // +0x208
};

// The per-tick road sample (from the native wrapper).
struct SampleInput {
    bool hasHit = false;
    bool failed = false;
    float yawFrom = 0.0f;
    float hitX = 0.0f, hitY = 0.0f, hitZ = 0.0f;
    int fromId = -1; // S_HorseRoadPoint.m_id
    int toId = -1;
};

struct State {
    // S_HorseRoadFollow: the persistent top-level road-follow latch.
    bool latched = false;

    // OnPress controller fields (+0x20..+0x2C).
    float deactivateTime = 0.0f;  // +0x20
    float reactivateTime = 0.0f;  // +0x24
    float hintTime = 0.0f;        // +0x28
    // +0x2C flags: bit0 action-controlled active, bit1 sample-hit mirror,
    // bit2 second engage bit (native teardown clears it with bit0), bit3
    // flick/failure, bit4 interruption armed.
    unsigned char flags = 0;

    // Backtrack history (pathB): rolling last-10 road-point ids with the
    // native dedup order.
    static constexpr int kPathCapacity = 10;
    int pathB[kPathCapacity] = {};
    int pathBCount = 0;

    // Publish outputs (the native S_HorseData fields).
    bool magnetismLive = false;
    float magnetYaw = 0.0f;
    float magnetHitX = 0.0f, magnetHitY = 0.0f, magnetHitZ = 0.0f;
    // UpdateTurnParams output (the sharp-corner slowdown flag). The native
    // computes it from the RoadSnap cart-box test (entity containers), which
    // is not portable to the pure machine; the port leaves it false and the
    // foot adapter does not consume the horse turn params.
    bool fastStop = false;

    void Reset();
};

// The recovered HintsActive predicate (0x180A4E99C).
bool HintsActive(const State& st);

// The hold-E action mapping: bit0 = the action-controlled active state
// (engage sets it, the deactivate hint clears it).
void SetActionActive(State& st, bool active);

// PushPathB (0x180A4E39C): rolling last-10 with the native dedup order
// (skip when last == from-id; push to-id after from-id; no consecutive
// duplicates; drop the oldest when full).
void PushPathB(State& st, int fromId, int toId);

// The recovered per-tick step: phases 0 -> slot1 -> the common
// S_HorseRoadFollow release/success flow -> phase 1. The sampler and
// GetRoadDistance run in the game-facing wrapper; this takes the sample.
//
//  manualInputHeld  Henry's WASD held (the foot equivalent of the rider
//                   stick; the phase-2 arm threshold).
//  chatFollow       the chat-follow gate (phase 0/1 teardown).
//  jumpRequest      the jump teardown (phase 1).
void StepState(State& st, const SampleInput& sample, const Cvars& cvars,
               float dt, bool manualInputHeld, bool chatFollow,
               bool jumpRequest);

} // namespace AutoWalk::RoadFollowPort
