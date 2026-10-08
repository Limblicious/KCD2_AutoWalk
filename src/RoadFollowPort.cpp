#include "RoadFollowPort.h"

#include "FootRoad.h"

namespace AutoWalk::RoadFollowPort {

namespace {

State g_state;
Cvars g_cvars;

// Foot-facade compensation constants (documented adaptations, not native
// constants): the standalone facade has no road cache, so the road query
// loses the road far more easily than the mounted horse does. The sampling
// radius is scaled up and brief sample gaps keep the latch alive while the
// last road direction persists -- the "spongier" grab the horse gets from
// its cache.
constexpr float kRoadRadiusScale = 2.0f;
constexpr int kMissGraceFrames = 30; // ~0.5s at 60 Hz

int g_missCounter = 0;
SampleInput g_lastSample{};
bool g_lastSampleValid = false;

} // namespace

void SetActionActive(bool active)
{
    SetActionActive(g_state, active);
    if (!active) {
        g_missCounter = 0;
    }
}

void Tick(float dt, const NativeMagnetism::FrameCVars& cvars,
          bool manualInputHeld, bool chatFollow,
          FootRoad::FootRoadProbe& outSample)
{
    // GetRoadDistance(latched): the OnPress acquisition/remain radius,
    // scaled up for the cache-less foot facade.
    g_cvars.roadDistOff = cvars.roadDistOff * kRoadRadiusScale;
    g_cvars.roadDistOn = cvars.roadDistOn * kRoadRadiusScale;
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

    // The miss grace: while latched, a brief road-query gap feeds the last
    // hit through so the machine stays latched and keeps commanding the
    // last road direction (the horse's cache-equivalent persistence).
    // Sustained off-road (beyond the grace) lets the machine deactivate
    // exactly as recovered.
    if (sample.hasHit) {
        g_missCounter = 0;
        g_lastSample = sample;
        g_lastSampleValid = true;
    } else if (g_state.latched && g_lastSampleValid &&
               g_missCounter < kMissGraceFrames) {
        ++g_missCounter;
        sample.hasHit = true;
        sample.yawFrom = g_lastSample.yawFrom;
        sample.hitX = g_lastSample.hitX;
        sample.hitY = g_lastSample.hitY;
        sample.hitZ = g_lastSample.hitZ;
        sample.fromId = -1;
        sample.toId = -1;
        // The probe keeps the real (missing) state for diagnostics.
    } else {
        g_missCounter = 0;
    }

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
