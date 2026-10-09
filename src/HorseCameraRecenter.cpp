#include "HorseCameraRecenter.h"

#include <algorithm>
#include <atomic>
#include <chrono>

#include "Log.h"
#include "NativeMagnetism.h"
#include "CryEngine/CryCommon/smartptr.h"
#include "entitymodule/C_ActorPhysicsState.h"
#include "entitymodule/C_Player.h"
#include "game/C_FocusCamera.h"

namespace AutoWalk::HorseCameraRecenter {
namespace {

constexpr float kDegToRad = 0.01745329252f;
float g_centerBlend = 0.0f;
std::atomic<long long> g_lastLookMs{0};
std::chrono::steady_clock::time_point g_lastLog{};

long long NowMs()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(
               steady_clock::now().time_since_epoch())
        .count();
}

void LogState(std::string_view gate, float lookPitch, float lookYaw,
              float blend, uint32_t focusFlags, float travelYaw,
              float viewZ, float accumZ, float centering, float centeringTime,
              float bodyYaw, float moveYaw, float cameraYaw)
{
    const auto now = std::chrono::steady_clock::now();
    if (g_lastLog != std::chrono::steady_clock::time_point{} &&
        now - g_lastLog < std::chrono::seconds(2)) {
        return;
    }
    g_lastLog = now;
    Log::Write(std::string("[AutoWalk] camera: gate=") + std::string(gate) +
               " lookPitch=" + std::to_string(lookPitch) +
               " lookYaw=" + std::to_string(lookYaw) +
               " blend=" + std::to_string(blend) +
               " focusFlags=" + std::to_string(focusFlags) +
               " centering=" + std::to_string(centering) +
               " centeringTime=" + std::to_string(centeringTime) +
               " sinceLookMs=" +
               std::to_string(NowMs() - g_lastLookMs.load()) +
               " travel=" + std::to_string(travelYaw) +
               " viewZ=" + std::to_string(viewZ) +
               " accumZ=" + std::to_string(accumZ) +
               " body=" + std::to_string(bodyYaw) +
               " move=" + std::to_string(moveYaw) +
               " cam=" + std::to_string(cameraYaw));
}

} // namespace

void Reset()
{
    g_centerBlend = 0.0f;
}

void NotifyMouseLook()
{
    g_lastLookMs.store(NowMs());
}

void Update(wh::entitymodule::C_Player* player, float travelYaw, float dt,
            float lookPitch, float lookYaw,
            float bodyYaw, float moveYaw, float cameraYaw)
{
    const NativeMagnetism::FrameCVars* cvars = nullptr;
    const bool cvarsOk = NativeMagnetism::RefreshFrameCVars(cvars) && cvars;
    const float centering =
        cvarsOk ? cvars->cameraCentering : -1.0f;
    const float centeringTime =
        cvarsOk ? cvars->cameraCenteringTime : 0.0f;
    const uint32_t focusFlags =
        player && player->m_pFocusCamera ? player->m_pFocusCamera->m_flags : 0;

    if (!player || !player->m_pPhysicsState || !cvarsOk || centering < 0.0f) {
        LogState("cvars", lookPitch, lookYaw, g_centerBlend, focusFlags,
                 travelYaw, 0.0f, 0.0f, centering, centeringTime,
                 bodyYaw, moveYaw, cameraYaw);
        Reset();
        return;
    }

    // The native mounted routine yields while C_FocusCamera owns the view.
    if (player->m_pFocusCamera && (focusFlags & 0x02U) != 0) {
        LogState("focus", lookPitch, lookYaw, g_centerBlend, focusFlags,
                 travelYaw, 0.0f, 0.0f, centering, centeringTime,
                 bodyYaw, moveYaw, cameraYaw);
        Reset();
        return;
    }

    // Native gate: MountedCamera_Centering restarts from the last look-input
    // frame and stays idle for CameraCenteringTime before pulling. On foot,
    // the look input is recorded by NotifyMouseLook; the movement request's
    // computed turn terms are NOT user input and are ignored here.
    const long long sinceLookMs = NowMs() - g_lastLookMs.load();
    if (centeringTime > 0.0f &&
        sinceLookMs < static_cast<long long>(centeringTime * 1000.0f)) {
        g_centerBlend = 0.0f;
        LogState("delay", lookPitch, lookYaw, g_centerBlend, focusFlags,
                 travelYaw, 0.0f, 0.0f, centering, centeringTime,
                 bodyYaw, moveYaw, cameraYaw);
        return;
    }

    auto* state = player->m_pPhysicsState;
    const Quat current = state->m_viewRotation;
    const float pitch = cvars->cameraCenteringPitchOffset * kDegToRad;

    // Native target construction is entityRotation * relativeYaw *
    // pitchOffset. The foot adapter's autonomous travel frame replaces the
    // horse entity yaw; no additional road-yaw term is needed.
    const Quat desired = Quat::CreateRotationZ(travelYaw) *
                         Quat::CreateRotationX(pitch);

    g_centerBlend = std::min(1.0f,
                             g_centerBlend + cvars->cameraCentering * dt);
    Quat target = desired;
    if (g_centerBlend < 1.0f) {
        target.SetSlerp(current, desired, g_centerBlend);
    }

    // REL 56442 computes inverse(currentView) * slerp(currentView, target,
    // blend), converts to Euler, and adds all three components to the
    // accumulator consumed and cleared by the actor physics tick.
    state->m_lookAngleAccum += Ang3((!current) * target);

    const Ang3 curAngles = Ang3(current);
    LogState("apply", lookPitch, lookYaw, g_centerBlend, focusFlags,
             travelYaw, curAngles.z, state->m_lookAngleAccum.z, centering,
             centeringTime, bodyYaw, moveYaw, cameraYaw);
}

} // namespace AutoWalk::HorseCameraRecenter
