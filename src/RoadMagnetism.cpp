#include "RoadMagnetism.h"

#include <sstream>

#include "crysystem/CCryAction.h"
#include "entitymodule/C_Horse.h"
#include "entitymodule/C_Player.h"
#include "entitymodule/C_RiderPlayerControl.h"
#include "entitymodule/C_RiderPlayerInput.h"
#include "entitymodule/S_HorseData.h"

namespace AutoWalk::RoadMagnetism {

MountedProbe ProbeMountedHorse()
{
    MountedProbe result{};

    auto* framework = CCryAction::GetInstance();
    auto* player = framework
        ? static_cast<wh::entitymodule::C_Player*>(framework->GetClientActor())
        : nullptr;

    if (!player) {
        return result;
    }
    result.playerFound = true;

    auto* riderControl = player->m_pRiderPlayerControl;
    auto* riderInput = riderControl ? riderControl->m_pPlayerInput : nullptr;
    auto* horse = riderInput ? riderInput->m_pHorse : nullptr;
    if (!horse) {
        return result;
    }
    result.mounted = true;

    auto* data = horse->m_pHorseData;
    if (!data) {
        return result;
    }
    result.horseDataFound = true;

    result.magnetismLive = data->m_magnetismLive;
    result.rawYaw = data->m_magnetYaw;
    result.smoothedYaw = data->m_yawSmoothed;
    result.hitX = data->m_magnetHit.x;
    result.hitY = data->m_magnetHit.y;
    result.hitZ = data->m_magnetHit.z;
    result.mode = data->m_roadFollow.m_mode;
    result.controllerPresent = data->m_roadFollow.m_pMagnetism != nullptr;

    return result;
}

std::string Describe(const MountedProbe& probe)
{
    std::ostringstream out;
    out << "mountedProbe"
        << " player=" << probe.playerFound
        << " mounted=" << probe.mounted
        << " horseData=" << probe.horseDataFound
        << " magnetismLive=" << probe.magnetismLive
        << " controller=" << probe.controllerPresent
        << " mode=" << probe.mode
        << " rawYaw=" << probe.rawYaw
        << " smoothedYaw=" << probe.smoothedYaw
        << " hit=(" << probe.hitX << "," << probe.hitY << "," << probe.hitZ << ")";
    return out.str();
}

} // namespace AutoWalk::RoadMagnetism
