#include "HorseCameraRecenter.h"

#include <atomic>
#include <chrono>
#include <cstdio>
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
constexpr float kRadToDeg = 57.295779513f;
float g_centerBlend = 0.0f;
std::atomic<long long> g_lastLookMs{0};
std::atomic<long long> g_lookEvents{0};
std::atomic<bool> g_lookMonitoring{false};
std::chrono::steady_clock::time_point g_lastLog{};
float g_prevTarget = 0.0f;
bool g_prevTargetValid = false;

long long NowMs()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(
               steady_clock::now().time_since_epoch())
        .count();
}

// Signed angular error in degrees of one reference-frame prediction against
// the measured displacement heading; "n/a" when no measurement exists.
std::string ErrStr(float predicted, float measured, bool have)
{
    if (!have) {
        return "n/a";
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.2f",
                  WrapPi(predicted - measured) * kRadToDeg);
    return buf;
}

void LogState(std::string_view gate, float lookPitch, float lookYaw,
              float blend, uint32_t focusFlags, float centering,
              float centeringTime, float viewYaw, float accumZ,
              float targetWorldYaw, const GroundTruth& gt)
{
    const auto now = std::chrono::steady_clock::now();
    if (g_lastLog != std::chrono::steady_clock::time_point{} &&
        now - g_lastLog < std::chrono::seconds(1)) {
        return;
    }

    // Target stability: signed change of the composed world target since the
    // previous logged sample, in degrees per second. A small value means the
    // camera is chasing a nearly stationary target (no feedback runaway).
    std::string tgtDelta = "n/a";
    if (g_prevTargetValid && g_lastLog != std::chrono::steady_clock::time_point{}) {
        const float elapsedSec =
            std::chrono::duration<float>(now - g_lastLog).count();
        if (elapsedSec > 0.05f) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.2f",
                          WrapPi(targetWorldYaw - g_prevTarget) * kRadToDeg /
                              elapsedSec);
            tgtDelta = buf;
        }
    }
    g_prevTarget = targetWorldYaw;
    g_prevTargetValid = true;
    g_lastLog = now;

    const bool have = gt.moveValid || gt.windowValid;
    const float measured = gt.windowValid ? gt.windowYaw : gt.moveYaw;
    const std::string src =
        gt.windowValid ? "win" : (gt.moveValid ? "frame" : "none");

    Log::Write(std::string("[AutoWalk] camera: gate=") + std::string(gate) +
               " blend=" + std::to_string(blend) +
               " focusFlags=" + std::to_string(focusFlags) +
               " centering=" + std::to_string(centering) +
               " centeringTime=" + std::to_string(centeringTime) +
               " sinceLookMs=" + std::to_string(NowMs() - g_lastLookMs.load()) +
               " lookEvents=" + std::to_string(g_lookEvents.load()) +
               " travel=" + std::to_string(gt.travelYaw) +
               " req=" + std::to_string(gt.requestYaw) +
               " reqVanilla=" + std::to_string(gt.vanillaRequestYaw) +
               " flat=" + std::to_string(gt.flatYaw) +
               " view=" + std::to_string(viewYaw) +
               " ent=" + std::to_string(gt.entityYaw) +
               " cam=" + std::to_string(gt.cameraYaw) +
               " target=" + std::to_string(targetWorldYaw) +
               " tgtErrCam=" +
               ErrStr(targetWorldYaw, gt.cameraYaw, true) +
               " tgtDeltaDps=" + tgtDelta +
               " move=" + (gt.moveValid ? std::to_string(gt.moveYaw) : "n/a") +
               " moveWin=" +
               (gt.windowValid ? std::to_string(gt.windowYaw) : "n/a") +
               " dxy=" + std::to_string(gt.dx) + "," + std::to_string(gt.dy) +
               " dt=" + std::to_string(gt.dt) +
               " measSrc=" + src +
               " errA=" + ErrStr(gt.travelYaw, measured, have) +
               " errB=" +
               ErrStr(WrapPi(gt.flatYaw + gt.requestYaw), measured, have) +
               " errC=" +
               ErrStr(WrapPi(gt.entityYaw + gt.requestYaw), measured, have) +
               " errD=" +
               ErrStr(WrapPi(viewYaw + gt.requestYaw), measured, have) +
               " errE=" +
               ErrStr(WrapPi(gt.cameraYaw + gt.requestYaw), measured, have));
}

} // namespace

void Reset()
{
    g_centerBlend = 0.0f;
    // Drop the previous logged target so tgtDeltaDps cannot span separate
    // follow sessions after a disengage.
    g_prevTargetValid = false;
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

void Update(wh::entitymodule::C_Player* player, float dt,
            float lookPitch, float lookYaw, const GroundTruth& gt)
{
    const NativeMagnetism::FrameCVars* cvars = nullptr;
    const bool cvarsOk = NativeMagnetism::RefreshFrameCVars(cvars) && cvars;
    const float centering = cvarsOk ? cvars->cameraCentering : -1.0f;
    const float centeringTime = cvarsOk ? cvars->cameraCenteringTime : 0.0f;
    const uint32_t focusFlags =
        player && player->m_pFocusCamera ? player->m_pFocusCamera->m_flags : 0;
    const float targetWorldYaw = WrapPi(gt.flatYaw + gt.travelYaw);

    if (!player || !player->m_pPhysicsState || !cvarsOk || centering < 0.0f) {
        LogState("cvars", lookPitch, lookYaw, g_centerBlend, focusFlags,
                 centering, centeringTime, 0.0f, 0.0f, targetWorldYaw, gt);
        Reset();
        return;
    }

    // Fail-closed: without a registered look-input monitor the recenter
    // could never be interrupted by the player.
    if (!g_lookMonitoring.load()) {
        LogState("monitor", lookPitch, lookYaw, g_centerBlend, focusFlags,
                 centering, centeringTime, 0.0f, 0.0f, targetWorldYaw, gt);
        Reset();
        return;
    }

    // The native mounted routine yields while C_FocusCamera owns the view.
    if (player->m_pFocusCamera && (focusFlags & 0x02U) != 0) {
        LogState("focus", lookPitch, lookYaw, g_centerBlend, focusFlags,
                 centering, centeringTime, 0.0f, 0.0f, targetWorldYaw, gt);
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
                 centering, centeringTime, 0.0f, 0.0f, targetWorldYaw, gt);
        return;
    }

    auto* state = player->m_pPhysicsState;
    const Quat current = state->m_viewRotation;
    const float pitch = cvars->cameraCenteringPitchOffset * kDegToRad;

    // Proportional pull: velocity = error * gain, capped. Fast initial swing
    // with decelerating approach and continuous curve tracking (the native
    // blend stage's constant-rate ramp is superseded; see REVERSE_ENGINEERING
    // and the user-requested feel). gain/maxRate are fractions of the native
    // road-follow smoother rate (rotationMax) so its compensation keeps up
    // with the pull -- the runtime-established stability constraint.
    const float gain = cvars->rotationMax * 0.6f;
    const float maxRate = cvars->rotationMax * 0.4f;
    const Ang3 curAngles = Ang3(current);
    const float pullYaw = PullVelocity(
        WrapPi(targetWorldYaw - curAngles.z), gain, maxRate,
        0.75f * kDegToRad);
    const float pullPitch = PullVelocity(
        pitch - curAngles.x, gain, maxRate, 0.75f * kDegToRad);

    if (pullYaw == 0.0f && pullPitch == 0.0f) {
        LogState("settled", lookPitch, lookYaw, g_centerBlend, focusFlags,
                 centering, centeringTime, curAngles.z,
                 state->m_lookAngleAccum.z, targetWorldYaw, gt);
        return;
    }

    state->m_lookAngleAccum += Ang3(pullPitch * dt, 0.0f, pullYaw * dt);

    LogState("apply", lookPitch, lookYaw, g_centerBlend, focusFlags,
             centering, centeringTime, curAngles.z,
             state->m_lookAngleAccum.z, targetWorldYaw, gt);
}

} // namespace AutoWalk::HorseCameraRecenter
