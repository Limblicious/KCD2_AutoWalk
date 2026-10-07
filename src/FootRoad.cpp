#include "FootRoad.h"

#include <cstring>
#include <sstream>
#include <type_traits>

#include "REL/Relocation.h"

#include "Log.h"
#include "Offsets/vtables/IEntity.h"
#include "crysystem/CCryAction.h"
#include "entitymodule/C_Horse.h"
#include "entitymodule/C_Player.h"
#include "entitymodule/C_RiderPlayerControl.h"
#include "entitymodule/C_RiderPlayerInput.h"
#include "entitymodule/S_HorseData.h"
#include "entitymodule/S_HorseRoadFollow.h"
#include "entitymodule/S_HorseMagnetismSample.h"
#include "entitymodule/S_HorseRoadPoint.h"

namespace AutoWalk::FootRoad {
namespace {

// KCD2 Steam 1.5.6 (kcd_addresslib_steam_release_1_5-15693.bin).
// See docs/REVERSE_ENGINEERING.md, Open item A.
constexpr REL::ID kIdRoadSampleWrapper{194146}; // sub_181ED8860

// bool wrapper(S_HorseRoadFollow* self, <unused rdx>, float searchDistance,
//              S_HorseMagnetismSample* out)
// Verified x64 register use at the S_HorseRoadFollow::Tick call site.
using RoadSampleWrapperFn = bool (*)(wh::entitymodule::S_HorseRoadFollow* self,
                                     void* unusedSecondArg,
                                     float searchDistance,
                                     wh::entitymodule::S_HorseMagnetismSample* out);

void CopySampleIntoProbe(const wh::entitymodule::S_HorseMagnetismSample& sample,
                         bool ok,
                         FootRoadProbe& out)
{
    out.called = ok;
    out.hasHit = ok && sample.m_hasHit;
    out.hitX = sample.m_hit.x;
    out.hitY = sample.m_hit.y;
    out.hitZ = sample.m_hit.z;
    out.alongX = sample.m_along.x;
    out.alongY = sample.m_along.y;
    out.alongZ = sample.m_along.z;
    out.yawFrom = sample.m_yawFrom;
    out.yawTo = sample.m_yawTo;
    out.pFromValid = sample.m_pFrom != nullptr;
    out.pToValid = sample.m_pTo != nullptr;
    out.failed = sample.m_failed != 0;
    if (sample.m_pFrom) {
        out.fromX = sample.m_pFrom->m_pos.x;
        out.fromY = sample.m_pFrom->m_pos.y;
        out.fromZ = sample.m_pFrom->m_pos.z;
    }
    if (sample.m_pTo) {
        out.toX = sample.m_pTo->m_pos.x;
        out.toY = sample.m_pTo->m_pos.y;
        out.toZ = sample.m_pTo->m_pos.z;
    }
}

struct CloneFacade {
    // Raw storage: no constructors/destructors may run on these copies
    // (their internal pointers are intentionally shallow/deep-patched and
    // must never be freed by the facade itself).
    std::aligned_storage_t<sizeof(wh::entitymodule::S_HorseData),
                           alignof(wh::entitymodule::S_HorseData)> horseData;
    std::aligned_storage_t<sizeof(wh::entitymodule::S_HorseRoadFollow),
                           alignof(wh::entitymodule::S_HorseRoadFollow)> roadFollow;
    alignas(8) unsigned char horse[0xA60]; // sizeof(C_Horse), see C_Horse.h

    // Owned deep copies of the two path-vector buffers; the facade's vector
    // headers are patched to point here so the probe can never touch live
    // game-owned buffers.
    float* pathABuffer = nullptr;
    float* pathBBuffer = nullptr;

    CloneFacade() = default;
    CloneFacade(const CloneFacade&) = delete;
    CloneFacade& operator=(const CloneFacade&) = delete;

    ~CloneFacade()
    {
        ::operator delete(pathABuffer);
        ::operator delete(pathBBuffer);
    }
};

// Patches the three MSVC std::vector pointers (begin/end/capacity, 8 bytes
// each; the plugin compiles with _ITERATOR_DEBUG_LEVEL=0 matching the game's
// release binary) of 'vec' to an owned deep copy of the live vector.
void DetachVector(std::vector<float>& vec, float*& ownedBuffer)
{
    using Ptr = float*;
    Ptr* slots = reinterpret_cast<Ptr*>(&vec);
    const auto begin = slots[0];
    const auto end = slots[1];
    const auto cap = slots[2];

    if (!cap) {
        return;
    }

    const auto count = static_cast<std::size_t>(end - begin);
    const auto capCount = static_cast<std::size_t>(cap - begin);
    ownedBuffer = static_cast<float*>(
        ::operator new(capCount * sizeof(float)));
    std::memcpy(ownedBuffer, begin, count * sizeof(float));

    slots[0] = ownedBuffer;
    slots[1] = ownedBuffer + count;
    slots[2] = ownedBuffer + capCount;
}

// Persistent synthetic facade shared by all standalone samples. Zeroed once;
// only the per-entity source (player entity) is refreshed per sample.
struct PersistentStandaloneFacade {
    std::aligned_storage_t<sizeof(wh::entitymodule::S_HorseData),
                           alignof(wh::entitymodule::S_HorseData)> horseData{};
    std::aligned_storage_t<sizeof(wh::entitymodule::S_HorseRoadFollow),
                           alignof(wh::entitymodule::S_HorseRoadFollow)> roadFollow{};
    alignas(8) unsigned char horse[0xA60]{};
    alignas(8) unsigned char roadCache[0xCC0]{}; // empty road-cache object
    bool initialized = false;

    void EnsureInitialized(Offsets::IEntity* playerEntity)
    {
        auto* hd = reinterpret_cast<wh::entitymodule::S_HorseData*>(&horseData);
        auto* rf = reinterpret_cast<wh::entitymodule::S_HorseRoadFollow*>(&roadFollow);

        *reinterpret_cast<Offsets::IEntity**>(horse + 0x38) = playerEntity;
        *reinterpret_cast<void**>(horse + 0x668) = roadCache;
        // C_Horse::m_pHorseData back-pointer (native helpers reach through it).
        *reinterpret_cast<wh::entitymodule::S_HorseData**>(horse + 0x9E8) = hd;
        rf->m_pHorseData = hd;
        rf->m_stick.m_pUser = &rf->m_stickUser;
        hd->m_pSelf = hd;
        hd->m_pHorse = reinterpret_cast<wh::entitymodule::C_Horse*>(horse);
        hd->m_pseudoSpeed = 3.0f;
        initialized = true;
    }
};

PersistentStandaloneFacade& GetPersistentFacade()
{
    static PersistentStandaloneFacade s_facade;
    return s_facade;
}

} // namespace

bool EnsureNativeRoadFollow()
{
    return RefreshStandaloneFacade();
}

// Hold-E latch request: mirrors the native OnPress SetHoldLatched (bit
// trivial); the tick latches on the next road hit and releases on a miss.
bool g_latchPending = false;
int g_missCounter = 0;
constexpr int kMissGraceFrames = 20; // ~0.33s at 60 Hz; the facade gap tolerance

bool NativeFollowReady()
{
    return GetPersistentFacade().initialized;
}

bool TickNativeRoadFollow(float dt, FootRoadProbe& out)
{
    out = FootRoadProbe{};

    auto& facade = GetPersistentFacade();
    if (!facade.initialized) {
        return false;
    }
    out.facadeBuilt = true;

    auto* rf = reinterpret_cast<wh::entitymodule::S_HorseRoadFollow*>(&facade.roadFollow);
    auto* hd = reinterpret_cast<wh::entitymodule::S_HorseData*>(&facade.horseData);
    wh::entitymodule::S_HorseMagnetismSample sample{};

    // The proven-safe wrapper call (REL 194146): the sample out register is
    // set immediately before the call, nothing between can clobber it.
    const auto fn = REL::Relocation<RoadSampleWrapperFn>(kIdRoadSampleWrapper).get();
    if (!fn) {
        Log::Write("[AutoWalk] footRoadNative: wrapper unresolved.");
        return false;
    }
    const bool ok = fn(rf, nullptr, 0.0f, &sample);
    CopySampleIntoProbe(sample, ok, out);

    // The recovered OnPress tick flow (REVERSE_ENGINEERING.md): a hit keeps
    // the latch and publishes; a miss releases. The mounted sampler rarely
    // misses (its road cache persists), so the mounted horse keeps
    // commanding the last road yaw and curves back onto the path. The
    // standalone facade loses the road more easily, so the latch survives
    // short gaps (grace window) while the last yaw command persists -- that
    // is the "return to path" behavior.
    const bool hit = ok && sample.m_hasHit;
    if (hit) {
        g_missCounter = 0;
        if (g_latchPending) {
            rf->m_latched = 1;
        }
        if (rf->m_latched) {
            hd->m_magnetismLive = 1;
            hd->m_magnetHit = sample.m_hit;
            hd->m_magnetYaw = sample.m_yawFrom;
        }
    } else if (rf->m_latched) {
        hd->m_magnetismLive = 0; // command persists; live flag clears
        ++g_missCounter;
        if (g_missCounter >= kMissGraceFrames) {
            rf->m_latched = 0;
            g_missCounter = 0;
        }
    }
    return ok;
}

bool NativeLatched()
{
    auto& facade = GetPersistentFacade();
    if (!facade.initialized) {
        return false;
    }
    return reinterpret_cast<wh::entitymodule::S_HorseRoadFollow*>(&facade.roadFollow)
               ->m_latched != 0;
}

bool NativeMagnetismLive()
{
    auto& facade = GetPersistentFacade();
    if (!facade.initialized) {
        return false;
    }
    return reinterpret_cast<wh::entitymodule::S_HorseData*>(&facade.horseData)
               ->m_magnetismLive != 0;
}

float NativeMagnetYaw()
{
    auto& facade = GetPersistentFacade();
    if (!facade.initialized) {
        return 0.0f;
    }
    return reinterpret_cast<wh::entitymodule::S_HorseData*>(&facade.horseData)
        ->m_magnetYaw;
}

// Hold-E latch request: mirrors the native OnPress SetHoldLatched (bit
// trivial); the tick latches on the next road hit and releases on a miss.
void NativeSetHoldLatched(bool latched)
{
    g_latchPending = latched;
    if (!latched) {
        auto& facade = GetPersistentFacade();
        if (facade.initialized) {
            reinterpret_cast<wh::entitymodule::S_HorseRoadFollow*>(&facade.roadFollow)
                ->m_latched = 0;
            reinterpret_cast<wh::entitymodule::S_HorseData*>(&facade.horseData)
                ->m_magnetismLive = 0;
        }
    }
}

FootRoadProbe ProbeRoadSample(float searchDistance)
{
    FootRoadProbe result{};

    auto* framework = CCryAction::GetInstance();
    auto* player = framework
        ? static_cast<wh::entitymodule::C_Player*>(framework->GetClientActor())
        : nullptr;
    if (!player) {
        Log::Write("[AutoWalk] footRoadProbe: no client player.");
        return result;
    }
    result.playerFound = true;

    auto* riderControl = player->m_pRiderPlayerControl;
    auto* riderInput = riderControl ? riderControl->m_pPlayerInput : nullptr;
    auto* horse = riderInput ? riderInput->m_pHorse : nullptr;
    if (!horse) {
        Log::Write("[AutoWalk] footRoadProbe: no mounted horse to seed the facade from.");
        return result;
    }
    result.mounted = true;

    auto* data = horse->m_pHorseData;
    if (!data) {
        Log::Write("[AutoWalk] footRoadProbe: horse has no S_HorseData.");
        return result;
    }
    result.horseDataFound = true;

    auto* playerEntity = framework->GetClientEntity();
    if (!playerEntity) {
        Log::Write("[AutoWalk] footRoadProbe: client entity unavailable.");
        return result;
    }

    CloneFacade facade;
    auto* facadeHorseData =
        reinterpret_cast<wh::entitymodule::S_HorseData*>(&facade.horseData);
    auto* facadeRoadFollow =
        reinterpret_cast<wh::entitymodule::S_HorseRoadFollow*>(&facade.roadFollow);

    std::memcpy(facadeHorseData, data, sizeof(facade.horseData));
    std::memcpy(facadeRoadFollow, &data->m_roadFollow, sizeof(facade.roadFollow));
    std::memcpy(facade.horse, data->m_pHorse, sizeof(facade.horse));

    DetachVector(facadeRoadFollow->m_pathA, facade.pathABuffer);
    DetachVector(facadeRoadFollow->m_pathB, facade.pathBBuffer);

    // Redirect the facade's per-entity sources to the player.
    *reinterpret_cast<Offsets::IEntity**>(facade.horse + 0x38) = playerEntity;
    facadeRoadFollow->m_stick.m_pUser = &facadeRoadFollow->m_stickUser;
    facadeRoadFollow->m_pHorseData = facadeHorseData;
    facadeHorseData->m_pSelf = facadeHorseData;
    facadeHorseData->m_pHorse = reinterpret_cast<wh::entitymodule::C_Horse*>(facade.horse);

    result.facadeBuilt = true;

    wh::entitymodule::S_HorseMagnetismSample sample{};
    const bool ok = REL::Relocation<RoadSampleWrapperFn>(kIdRoadSampleWrapper)
                        .get()(facadeRoadFollow, nullptr, searchDistance, &sample);

    CopySampleIntoProbe(sample, ok, result);
    return result;
}

FootRoadProbe ProbeRoadSampleStandalone(float searchDistance)
{
    FootRoadProbe result{};

    auto* framework = CCryAction::GetInstance();
    auto* player = framework
        ? static_cast<wh::entitymodule::C_Player*>(framework->GetClientActor())
        : nullptr;
    if (!player) {
        Log::Write("[AutoWalk] footRoadStandalone: no client player.");
        return result;
    }
    result.playerFound = true;

    auto* playerEntity = framework->GetClientEntity();
    if (!playerEntity) {
        Log::Write("[AutoWalk] footRoadStandalone: client entity unavailable.");
        return result;
    }

    auto& facade = GetPersistentFacade();
    facade.EnsureInitialized(playerEntity);
    result.facadeBuilt = true;

    auto* facadeRoadFollow =
        reinterpret_cast<wh::entitymodule::S_HorseRoadFollow*>(&facade.roadFollow);
    wh::entitymodule::S_HorseMagnetismSample sample{};
    const bool ok = REL::Relocation<RoadSampleWrapperFn>(kIdRoadSampleWrapper)
                        .get()(facadeRoadFollow, nullptr, searchDistance, &sample);

    CopySampleIntoProbe(sample, ok, result);
    return result;
}

bool SampleRoadStandalone(float searchDistance, FootRoadProbe& out)
{
    out = FootRoadProbe{};

    auto* framework = CCryAction::GetInstance();
    auto* player = framework
        ? static_cast<wh::entitymodule::C_Player*>(framework->GetClientActor())
        : nullptr;
    if (!player) {
        return false;
    }
    out.playerFound = true;

    auto* playerEntity = framework->GetClientEntity();
    if (!playerEntity) {
        return false;
    }

    auto& facade = GetPersistentFacade();
    facade.EnsureInitialized(playerEntity);
    out.facadeBuilt = true;

    auto* facadeRoadFollow =
        reinterpret_cast<wh::entitymodule::S_HorseRoadFollow*>(&facade.roadFollow);
    wh::entitymodule::S_HorseMagnetismSample sample{};
    const bool ok = REL::Relocation<RoadSampleWrapperFn>(kIdRoadSampleWrapper)
                        .get()(facadeRoadFollow, nullptr, searchDistance, &sample);

    CopySampleIntoProbe(sample, ok, out);
    return ok;
}

wh::entitymodule::S_HorseData* GetStandaloneHorseData()
{
    return reinterpret_cast<wh::entitymodule::S_HorseData*>(
        &GetPersistentFacade().horseData);
}

wh::entitymodule::S_HorseRoadFollow* GetStandaloneRoadFollow()
{
    return reinterpret_cast<wh::entitymodule::S_HorseRoadFollow*>(
        &GetPersistentFacade().roadFollow);
}

bool RefreshStandaloneFacade()
{
    auto* framework = CCryAction::GetInstance();
    auto* player = framework
        ? static_cast<wh::entitymodule::C_Player*>(framework->GetClientActor())
        : nullptr;
    if (!player) {
        return false;
    }
    auto* playerEntity = framework->GetClientEntity();
    if (!playerEntity) {
        return false;
    }
    GetPersistentFacade().EnsureInitialized(playerEntity);
    return true;
}

std::string Describe(const FootRoadProbe& probe)
{
    std::ostringstream out;
    out << "footRoadProbe"
        << " player=" << probe.playerFound
        << " mounted=" << probe.mounted
        << " horseData=" << probe.horseDataFound
        << " facade=" << probe.facadeBuilt
        << " called=" << probe.called
        << " hasHit=" << probe.hasHit
        << " hit=(" << probe.hitX << "," << probe.hitY << "," << probe.hitZ << ")"
        << " along=(" << probe.alongX << "," << probe.alongY << "," << probe.alongZ << ")"
        << " yawFrom=" << probe.yawFrom
        << " yawTo=" << probe.yawTo
        << " pFrom=" << probe.pFromValid
        << " pTo=" << probe.pToValid
        << " from=(" << probe.fromX << "," << probe.fromY << "," << probe.fromZ << ")"
        << " to=(" << probe.toX << "," << probe.toY << "," << probe.toZ << ")";
    return out.str();
}

} // namespace AutoWalk::FootRoad
