#pragma once

#include "NativeMagnetism.h"

namespace AutoWalk::FootRoad {
struct FootRoadProbe;
}

namespace AutoWalk::RoadFollowPort {

// Faithful C++ port of the recovered S_HorseRoadFollow::Tick flow and the
// S_OnPressController/S_AutoController acceptance gates, running around the
// proven native road sampler. This module owns the ROAD-FOLLOWING state
// machine only; it does not touch the camera, the movement hook, or the
// input seam (Track B, separate from the camera/body Track A).
//
// See docs/REVERSE_ENGINEERING.md:
//   - S_HorseRoadFollow::Tick (0x180A4E5AC)
//   - S_AutoController::SetHoldLatched gates (0x1829F1C70)
//   - S_OnPressController phases (0x180A4E768)
//   - PathHistory (0x181ECA180 prune / 0x181ECA440 flick score)
//   - HorseRoadFollow_PushPathB (0x180A4E39C) / PublishMagnetism
//     (0x180A4DE5C) / UpdateTurnParams (0x180A4DFF4)

struct State {
    // S_HorseRoadFollow persistence.
    bool latched = false;

    // OnPress controller fields (+0x20..+0x2C).
    float deactivateTime = 0.0f;
    float reactivateTime = 0.0f;
    float hintTime = 0.0f;
    unsigned char flags = 0; // bit0 active, bit1 latched, bit3 flick, bit4 armed

    // Path history: {flag,time} records inside the SnapTime window.
    static constexpr int kHistoryCapacity = 32;
    unsigned char historyFlags[kHistoryCapacity] = {};
    float historyTimes[kHistoryCapacity] = {};
    int historyCount = 0;

    // Backtrack history (pathB): road-point ids, capped at 10.
    static constexpr int kPathCapacity = 10;
    int pathB[kPathCapacity] = {};
    int pathBCount = 0;

    // Publish outputs (the native S_HorseData fields).
    bool magnetismLive = false;
    float magnetYaw = 0.0f; // the accepted road command (sample.yawFrom)

    void Reset();
};

// Engage/disengage via the hold-E latch (the OnPress SetHoldLatched: the
// latched bit; the gates decide acceptance).
void SetHoldLatched(bool latched);

// One recovered tick. Runs the native sampler with the native acquisition
// radius (GetRoadDistance -> RoadMagnetismOnPressRoadDistOff/On),
// the acceptance gates, the controller phases, the path history, and the
// publish step. Returns the resulting latched state.
//
//  dt           frame delta (clamped by the caller)
//  cvars        the native frame-helper cvars (enter/remain/snap/timers...)
//  manualInput  Henry's WASD held (the foot equivalent of the rider stick)
//  chatFollow   the chat-follow active gate
//  outSample    the native sample (for diagnostics)
State Tick(float dt, const NativeMagnetism::FrameCVars& cvars,
           bool manualInput, bool chatFollow,
           FootRoad::FootRoadProbe& outSample);

// The recovered controller phases (0..5) shared with Tick.
void ControllerTick(State& st, const FootRoad::FootRoadProbe& sample,
                    int phase, float dt, const NativeMagnetism::FrameCVars& cvars);

// The current ported state (for diagnostics/consumers).
State GetState();

} // namespace AutoWalk::RoadFollowPort
