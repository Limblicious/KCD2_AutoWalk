#include "HorseCameraRecenter.h"

#include <atomic>
#include <chrono>
#include <string>

#include "CameraRecenterMath.h"
#include "CameraRecenterTiming.h"
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
std::atomic<long long> g_lookEvents{0};
std::atomic<bool> g_lookMonitoring{false};
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
              float bodyYaw, float moveYaw, bool moveValid, float cameraYaw)
{
    const auto now = std::chrono::steady_clock::now();
    if (g_lastLog != std::chrono::steady_clock::time_point{} &&
        now - g_lastLog < std::chrono::seconds(2)) {
        return;
    }
    g_lastLog = now;
    const std::string move = moveValid ? std::to_string(moveYaw) : "n/a";
    Log::Write(std::string("[AutoWalk] camera: gate=") + std::string(gate) +
               " lookPitch=" + std::to_string(lookPitch) +
               " lookYaw=" + std::to_string(lookYaw) +
               " blend=" + std::to_string(blend) +
               " focusFlags=" + std::to_string(focusFlags) +
               " centering=" + std::to_string(centering) +
               " centeringTime=" + std::to_string(centeringTime) +
               " sinceLookMs=" +
               std::to_string(NowMs() - g_lastLookMs.load()) +
               " lookEvents=" + std::to_string(g_lookEvents.load()) +
               " travel=" + std::to_string(travelYaw) +
               " viewZ=" + std::to_string(viewZ) +
               " accumZ=" + std::to_string(accumZ) +
               " body=" + std::to_string(bodyYaw) +
               " move=" + move +
               " cam=" + std::to_string(cameraYaw));
}

} // namespace

void Reset()
{
    g_centerBlend = 0.0f;
}

void SetLookMonitoring(bool active)
{
    g_lookMonitoring.store(active);
}

void NotifyMouseLook()
{
    g_lastLookMs.store(NowMs());
    g_lookEvents.fetch_add(1);
}

void Update(wh::entitymodule::C_Player* player, float travelYaw, float dt,
            float lookPitch, float lookYaw,
            float bodyYaw, float moveYaw, bool moveValid, float cameraYaw)
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
                 bodyYaw, moveYaw, moveValid, cameraYaw);
        Reset();
        return;
    }

    // Fail-closed: without a registered look-input monitor the recenter
    // could never be interrupted by the player.
    if (!g_lookMonitoring.load()) {
        LogState("monitor", lookPitch, lookYaw, g_centerBlend, focusFlags,
                 travelYaw, 0.0f, 0.0f, centering, centeringTime,
                 bodyYaw, moveYaw, moveValid, cameraYaw);
        Reset();
        return;
    }

    // The native mounted routine yields while C_FocusCamera owns the view.
    if (player->m_pFocusCamera && (focusFlags & 0x02U) != 0) {
        LogState("focus", lookPitch, lookYaw, g_centerBlend, focusFlags,
                 travelYaw, 0.0f, 0.0f, centering, centeringTime,
                 bodyYaw, moveYaw, moveValid, cameraYaw);
        Reset();
        return;
    }

    // Native gate: MountedCamera_Centering restarts from the last look-input
    // frame and stays idle for CameraCenteringTime before pulling. On foot,
    // the look input is recorded by NotifyMouseLook; the movement request's
    // computed turn terms are NOT user input and are ignored here.
    const float sinceLookSec =
        static_cast<float>(NowMs() - g_lastLookMs.load()) / 1000.0f;
    if (!StepTiming(centering, centeringTime, sinceLookSec, dt,
                    g_centerBlend)) {
        LogState("delay", lookPitch, lookYaw, g_centerBlend, focusFlags,
                 travelYaw, 0.0f, 0.0f, centering, centeringTime,
                 bodyYaw, moveYaw, moveValid, cameraYaw);
        return;
    }

    auto* state = player->m_pPhysicsState;
    const Quat current = state->m_viewRotation;
    const float pitch = cvars->cameraCenteringPitchOffset * kDegToRad;
    const Quat desired = RecenterTarget(travelYaw, pitch);

    // REL 56442 computes inverse(currentView) * slerp(currentView, target,
    // blend), converts to Euler, and adds all three components to the
    // accumulator consumed and cleared by the actor physics tick.
    state->m_lookAngleAccum += RecenterDelta(current, desired, g_centerBlend);

    const Ang3 curAngles = Ang3(current);
    LogState("apply", lookPitch, lookYaw, g_centerBlend, focusFlags,
             travelYaw, curAngles.z, state->m_lookAngleAccum.z, centering,
             centeringTime, bodyYaw, moveYaw, moveValid, cameraYaw);
}

} // namespace AutoWalk::HorseCameraRecenter
