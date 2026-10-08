#include "RoadFollowPort.h"

#include "FootRoad.h"

namespace AutoWalk::RoadFollowPort {

namespace {

State g_state;
Cvars g_cvars;

} // namespace

void SetActionActive(bool active)
{
    SetActionActive(g_state, active);
}

void Tick(float dt, const NativeMagnetism::FrameCVars& cvars,
          bool manualInputHeld, bool chatFollow,
          FootRoad::FootRoadProbe& outSample)
{
    // GetRoadDistance(latched): the OnPress acquisition/remain radius.
    g_cvars.roadDistOff = cvars.roadDistOff;
    g_cvars.roadDistOn = cvars.roadDistOn;
    g_cvars.remainAngle = cvars.remainAngle;
    g_cvars.deactivateTime = cvars.deactivateTime;
    g_cvars.reactivateTime = cvars.reactivateTime;

    const bool latched = g_state.latched;
    FootRoad::SampleRoadStandalone(
        latched ? g_cvars.roadDistOn : g_cvars.roadDistOff, outSample);

    SampleInput sample{};
    sample.hasHit = outSample.hasHit;
    sample.failed = outSample.failed;
    sample.yawFrom = outSample.yawFrom;
    sample.hitX = outSample.hitX;
    sample.hitY = outSample.hitY;
    sample.hitZ = outSample.hitZ;
    sample.fromId = outSample.pFromValid ? outSample.fromId : -1;
    sample.toId = outSample.pToValid ? outSample.toId : -1;

    // The jump request (horseData+0x10C) has no on-foot equivalent; pass
    // false (documented adaptation -- Henry cannot jump while following).
    StepState(g_state, sample, g_cvars, dt, manualInputHeld, chatFollow,
              /*jumpRequest=*/false);
}

const State& GetState()
{
    return g_state;
}

} // namespace AutoWalk::RoadFollowPort
