#include "FollowController.h"

#include <atomic>
#include <chrono>
#include <cmath>

#include "FootRoad.h"
#include "InputSeam.h"
#include "Log.h"
#include "Offsets/vtables/IInput.h"
#include "Offsets/vtables/IInputEventListener.h"
#include "crysystem/CCryAction.h"
#include "crysystem/SSystemGlobalEnvironment.h"
#include "entitymodule/C_Player.h"
#include "entitymodule/C_RiderPlayerControl.h"
#include "entitymodule/C_RiderPlayerInput.h"

namespace AutoWalk::FollowController {
namespace {

std::atomic<Phase> g_phase{Phase::Disabled};
bool g_wHeld = false;

constexpr float kSampleDistance = 40.0f;
constexpr float kSampleIntervalSeconds = 1.0f / 15.0f; // 15 Hz road sampling
constexpr float kSmoothRate = 10.0f;  // exponential smoothing of the error
constexpr float kDeadzone = 0.004f;   // rad

std::chrono::steady_clock::time_point g_lastSample{};
std::chrono::steady_clock::time_point g_lastLog{};

// Latest road aim point + travel direction, with continuity filtering.
// The standalone sampler has no persistent road cache, so its from/to
// segment orientation can flip between queries; the direction continuity
// guard keeps the target stable.
struct RoadState {
    bool valid = false;
    float alongX = 0.0f, alongY = 0.0f;
    float hitX = 0.0f, hitY = 0.0f;
    float dirX = 0.0f, dirY = 0.0f;
};

RoadState g_road{};
float g_smoothedError = 0.0f;

// True while the current PostInputEvent call is our own synthetic W press;
// the input listener ignores those (they must not cancel the follow).
std::atomic<bool> g_syntheticPressInFlight{false};

struct QuatBuf {
    float x, y, z, w;
};

// IEntity slot [48] (vf+0x180): world rotation getter used by the native
// road state builder (verified via Offsets/vtables/IEntity.h + disassembly).
using GetWorldQuatFn = QuatBuf* (*)(void* entity, QuatBuf* out);

bool TryGetPlayerPose(float& posX, float& posY, float& fwdX, float& fwdY)
{
    auto* framework = CCryAction::GetInstance();
    auto* entity = framework ? framework->GetClientEntity() : nullptr;
    if (!entity) {
        return false;
    }

    Vec3 pos{};
    auto* iface = static_cast<Offsets::IEntity*>(entity);
    iface->GetWorldPos(pos);
    posX = pos.x;
    posY = pos.y;

    const auto vtable = *reinterpret_cast<std::uintptr_t*>(entity);
    const auto fn = *reinterpret_cast<GetWorldQuatFn*>(vtable + 0x180);
    if (!fn) {
        return false;
    }
    QuatBuf q{};
    const auto* result = fn(entity, &q);
    if (!result) {
        return false;
    }

    const float fx = 2.0f * (result->x * result->y + result->w * result->z);
    const float fy = 1.0f - 2.0f * (result->x * result->x + result->z * result->z);
    const float len = std::sqrt(fx * fx + fy * fy);
    if (len < 1e-6f) {
        return false;
    }
    fwdX = fx / len;
    fwdY = fy / len;
    return true;
}

bool IsMounted()
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

void ReleaseForward()
{
    if (g_wHeld && InputSeam::IsKeyHeld(Offsets::eKI_W)) {
        InputSeam::PostKeyEvent(Offsets::eKI_W, Offsets::eIS_Released, 0.0f);
    }
    g_wHeld = false;
}

void HoldForward()
{
    if (!InputSeam::IsKeyHeld(Offsets::eKI_W)) {
        g_syntheticPressInFlight.store(true);
        InputSeam::PostKeyEvent(Offsets::eKI_W, Offsets::eIS_Pressed, 1.0f);
        g_syntheticPressInFlight.store(false);
    }
    g_wHeld = true;
}

void SampleRoad()
{
    FootRoad::FootRoadProbe sample{};
    if (!FootRoad::SampleRoadStandalone(kSampleDistance, sample) ||
        !sample.hasHit) {
        g_road.valid = false;
        return;
    }

    float dirX = sample.alongX - sample.hitX;
    float dirY = sample.alongY - sample.hitY;
    const float len = std::sqrt(dirX * dirX + dirY * dirY);
    if (len < 1e-6f) {
        return;
    }
    dirX /= len;
    dirY /= len;

    float alongX = sample.alongX;
    float alongY = sample.alongY;

    // Continuity: a flip means the query reversed its segment orientation.
    // Reflect the aim point across the hit point so the target stays on the
    // same side of the player.
    if (g_road.valid) {
        const float dot = dirX * g_road.dirX + dirY * g_road.dirY;
        if (dot < -0.5f) {
            dirX = -dirX;
            dirY = -dirY;
            alongX = 2.0f * sample.hitX - alongX;
            alongY = 2.0f * sample.hitY - alongY;
        }
    }

    g_road.alongX = alongX;
    g_road.alongY = alongY;
    g_road.hitX = sample.hitX;
    g_road.hitY = sample.hitY;
    g_road.dirX = dirX;
    g_road.dirY = dirY;
    g_road.valid = true;
}

// Registered once with pInput->AddEventListener: cancels the follow when the
// player presses any movement key (W/A/S/D) that we did not synthesize.
class MovementCancelListener final : public Offsets::IInputEventListener {
public:
    bool OnInputEvent(const Offsets::SInputEvent& event) override
    {
        if (event.deviceId != Offsets::eDI_Keyboard ||
            g_phase.load() == Phase::Disabled) {
            return false;
        }

        const auto key = event.keyId;
        const bool isMoveKey = key == Offsets::eKI_W || key == Offsets::eKI_A ||
                               key == Offsets::eKI_S || key == Offsets::eKI_D;
        if (!isMoveKey) {
            return false;
        }

        if (key == Offsets::eKI_W && g_syntheticPressInFlight.load()) {
            return false; // our own press
        }

        if (event.state == Offsets::eIS_Pressed) {
            ReleaseForward();
            g_phase.store(Phase::Disabled);
            Log::Write("[AutoWalk] FollowController: cancelled by player input.");
        }
        return false; // never consume
    }

    bool OnInputEventUI(const void*) override { return false; }
    int GetPriority() const override { return 0; }
    bool _vf3(const void*) override { return false; }
};

MovementCancelListener s_cancelListener;
bool s_listenerRegistered = false;

void EnsureCancelListener()
{
    if (s_listenerRegistered) {
        return;
    }
    auto* env = SSystemGlobalEnvironment::GetInstance();
    if (env && env->pInput && env->pInput->AddEventListener(&s_cancelListener)) {
        s_listenerRegistered = true;
        Log::Write("[AutoWalk] FollowController: input cancel listener registered.");
    }
}

} // namespace

float g_steerGain = 12.0f;
float g_steerMax = 5.0f;
float g_steerLookahead = 0.35f;
int g_steerInvert = 0;

Phase GetPhase()
{
    return g_phase.load();
}

void Enable()
{
    EnsureCancelListener();
    g_phase.store(Phase::AwaitingRoad);
    g_smoothedError = 0.0f;
    g_road.valid = false;
    Log::Write("[AutoWalk] FollowController: enabled.");
}

void Disable()
{
    ReleaseForward();
    g_phase.store(Phase::Disabled);
    Log::Write("[AutoWalk] FollowController: disabled.");
}

void Reset()
{
    ReleaseForward();
    g_phase.store(Phase::Disabled);
}

void Tick()
{
    if (g_phase.load() == Phase::Disabled) {
        return;
    }

    auto* framework = CCryAction::GetInstance();
    auto* player = framework
        ? static_cast<wh::entitymodule::C_Player*>(framework->GetClientActor())
        : nullptr;

    if (!player || IsMounted()) {
        ReleaseForward();
        g_phase.store(Phase::Disabled);
        Log::Write("[AutoWalk] FollowController: stopped (player unavailable or mounted).");
        return;
    }

    using namespace std::chrono;
    const auto now = steady_clock::now();

    if (g_lastSample == steady_clock::time_point{} ||
        now - g_lastSample >= milliseconds(static_cast<long long>(kSampleIntervalSeconds * 1000.0f))) {
        g_lastSample = now;
        SampleRoad();
    }

    if (!g_road.valid) {
        g_phase.store(Phase::AwaitingRoad);
        ReleaseForward();
        return;
    }

    // User-input cancel (belt and braces): the listener may be skipped when
    // an earlier listener consumes events, so also poll the real-device key
    // state directly. We never synthesize A/S/D, so any down state is the
    // player steering manually.
    auto* env = SSystemGlobalEnvironment::GetInstance();
    if (env && env->pInput) {
        const bool userSteer =
            env->pInput->InputState("a", Offsets::eIS_Down) ||
            env->pInput->InputState("s", Offsets::eIS_Down) ||
            env->pInput->InputState("d", Offsets::eIS_Down);
        if (userSteer) {
            ReleaseForward();
            g_phase.store(Phase::Disabled);
            Log::Write("[AutoWalk] FollowController: cancelled by player steering (A/S/D).");
            return;
        }
    }

    float posX = 0.0f, posY = 0.0f, fwdX = 0.0f, fwdY = 0.0f;
    if (!TryGetPlayerPose(posX, posY, fwdX, fwdY)) {
        ReleaseForward();
        return;
    }

    // Steering: align the heading with the road tangent plus a small
    // cross-track correction pulling back toward the road line.
    const float headingErr = std::atan2(
        fwdX * g_road.dirY - fwdY * g_road.dirX,
        fwdX * g_road.dirX + fwdY * g_road.dirY);
    const float rx = posX - g_road.hitX;
    const float ry = posY - g_road.hitY;
    const float cte = rx * g_road.dirY - ry * g_road.dirX; // >0 = left of line
    float cteCorr = cte * 0.05f;
    if (cteCorr > 0.3f) cteCorr = 0.3f;
    if (cteCorr < -0.3f) cteCorr = -0.3f;
    const float error = headingErr + cteCorr;

    const float alpha = kSmoothRate * (1.0f / 60.0f);
    g_smoothedError += (error - g_smoothedError) * alpha;

    const float sign = g_steerInvert ? -1.0f : 1.0f;
    if (std::abs(g_smoothedError) > kDeadzone) {
        float dx = g_smoothedError * g_steerGain * sign;
        if (dx > g_steerMax) dx = g_steerMax;
        if (dx < -g_steerMax) dx = -g_steerMax;
        InputSeam::PostMouseDelta(dx);
    }

    HoldForward();
    g_phase.store(Phase::Following);

    if (g_lastLog == steady_clock::time_point{} ||
        now - g_lastLog >= seconds(1)) {
        g_lastLog = now;
        Log::Write(std::string("[AutoWalk] follow: headingErr=") +
                   std::to_string(headingErr) +
                   " cte=" + std::to_string(cte) +
                   " err=" + std::to_string(error) +
                   " smoothed=" + std::to_string(g_smoothedError) +
                   " fwd=(" + std::to_string(fwdX) + "," + std::to_string(fwdY) + ")" +
                   " dir=(" + std::to_string(g_road.dirX) + "," + std::to_string(g_road.dirY) + ")");
    }
}

} // namespace AutoWalk::FollowController
