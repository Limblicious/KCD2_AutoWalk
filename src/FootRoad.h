#pragma once

#include <string>

namespace wh::entitymodule {
class S_HorseData;
class S_HorseRoadFollow;
}

namespace AutoWalk::FootRoad {

struct FootRoadProbe {
    bool playerFound = false;
    bool mounted = false;
    bool horseDataFound = false;
    bool facadeBuilt = false;
    bool called = false;
    bool hasHit = false;
    float hitX = 0.0f;
    float hitY = 0.0f;
    float hitZ = 0.0f;
    float alongX = 0.0f;
    float alongY = 0.0f;
    float alongZ = 0.0f;
    float yawFrom = 0.0f;
    float yawTo = 0.0f;
    bool pFromValid = false;
    bool pToValid = false;
    bool failed = false;
    // Road segment endpoint positions (world).
    float fromX = 0.0f, fromY = 0.0f, fromZ = 0.0f;
    float toX = 0.0f, toY = 0.0f, toZ = 0.0f;
};

// Runs the native horse road-sampling chain (wrapper REL::ID 194146 ->
// sampler REL::ID 55123) against a clone facade seeded from the live
// mounted horse, with only the entity position/orientation source redirected
// to the player's IEntity.
//
// Read-only with respect to live game state: the sampler writes only into
// this probe's own output buffer. Fail-closed: no call is made unless every
// facade field could be seeded from a live mounted horse.
FootRoadProbe ProbeRoadSample(float searchDistance = 60.0f);

// Standalone (no horse) variant: a fully synthetic zeroed facade whose
// entity source is the player and whose road cache is an owned empty buffer.
// The road query itself is global and needs no horse state; the empty cache
// makes the native lookup fall back to its static default. Console-gated
// diagnostic.
FootRoadProbe ProbeRoadSampleStandalone(float searchDistance = 40.0f);

// Reusable standalone sample: fills 'out' from a persistent synthetic facade
// (allocated once). False when the player/entity is unavailable or the call
// failed; check out.hasHit for road presence.
bool SampleRoadStandalone(float searchDistance, FootRoadProbe& out);

// The persistent standalone facade's typed structures, for driving the
// native road-follow tick against synthetic on-foot state.
wh::entitymodule::S_HorseData* GetStandaloneHorseData();
wh::entitymodule::S_HorseRoadFollow* GetStandaloneRoadFollow();

// Re-points the persistent facade at the current player entity. False when
// no player/entity is available.
bool RefreshStandaloneFacade();

std::string Describe(const FootRoadProbe& probe);

} // namespace AutoWalk::FootRoad
