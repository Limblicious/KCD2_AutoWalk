#include "PlayerMovement.h"

#include "crysystem/CCryAction.h"
#include "entitymodule/C_Player.h"
#include "entitymodule/C_RiderPlayerControl.h"
#include "entitymodule/C_RiderPlayerInput.h"

namespace AutoWalk::PlayerMovement {

bool IsPlayerMounted()
{
    auto* framework = CCryAction::GetInstance();
    auto* player = framework
        ? static_cast<wh::entitymodule::C_Player*>(framework->GetClientActor())
        : nullptr;

    if (!player || !player->m_pRiderPlayerControl) {
        return false;
    }

    auto* input = player->m_pRiderPlayerControl->m_pPlayerInput;
    return input && input->m_pHorse;
}

} // namespace AutoWalk::PlayerMovement
