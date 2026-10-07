#include "FollowController.h"

#include <atomic>
#include <chrono>
#include <cmath>

#include "FootRoad.h"
#include "InputSeam.h"
#include "Log.h"
#include "NativeMagnetism.h"
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

std::chrono::steady_clock::time_point g_lastSample{};
std::chrono::steady_clock::time_point g_lastLog{};
std::chrono::steady_clock::time_point g_lastTick{};

NativeMagnetism::OnPressState g_state{};
NativeMagnetism::YawSmoother g_smoother{};
float g_prevSmoothedYaw = 0.0f;
float g_accumBaseline = 0.0f;

FootRoad::FootRoadProbe g_cachedSample{};
bool g_cachedSampleValid = false;

// User-input signals gathered by the input listener each frame.
std::atomic<bool> g_engageRequested{false};
std::atomic<bool> g_manualInput{false};

// True while the current PostInputEvent call is our own synthetic W press.
std::atomic<bool> g_syntheticPressInFlight{false};

struct QuatBuf {
    float x, y, z, w;
};

using GetWorldQuatFn = QuatBuf* (*)(void* entity, QuatBuf* out);

bool TryGetPlayerYaw(float& yawOut)
{
    auto* framework = CCryAction::GetInstance();
    auto* entity = framework ? framework->GetClientEntity() : nullptr;
    if (!entity) {
        return false;
    }

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
    yawOut = std::atan2(fy, fx);
    return true;
}

// Henry's C_ActorPhysicsState look accumulator (+0x238 -> +0x88), the same
// seam the mounted horse glue (REL 37998) writes to.
float* GetLookAccumulator()
{
    auto* framework = CCryAction::GetInstance();
    auto* player = framework
        ? static_cast<wh::entitymodule::C_Player*>(framework->GetClientActor())
        : nullptr;
    if (!player) {
        return nullptr;
    }
    const auto state = *reinterpret_cast<void**>(
        reinterpret_cast<std::uintptr_t>(player) + 0x238);
    if (!state) {
        return nullptr;
    }
    return reinterpret_cast<float*>(reinterpret_cast<std::uintptr_t>(state) + 0x88);
}

bool IsChatFollowActive()
{
    auto* framework = CCryAction::GetInstance();
    auto* player = framework
        ? static_cast<wh::entitymodule::C_Player*>(framework->GetClientActor())
        : nullptr;
    if (!player) {
        return false;
    }
    const auto chat = *reinterpret_cast<void**>(
        reinterpret_cast<std::uintptr_t>(player) + 0xCE8);
    if (!chat) {
        return false;
    }
    using ChatFollowFn = bool (*)(void*);
    const auto fn = *reinterpret_cast<ChatFollowFn*>(
        *reinterpret_cast<std::uintptr_t*>(chat) + 0x08);
    return fn ? fn(chat) : false;
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

class InputListener final : public Offsets::IInputEventListener {
public:
    bool OnInputEvent(const Offsets::SInputEvent& event) override
    {
        if (event.deviceId != Offsets::eDI_Keyboard) {
            return false;
        }

        const auto key = event.keyId;
        if (key == Offsets::eKI_E && event.state == Offsets::eIS_Pressed) {
            g_engageRequested.store(true);
            return false;
        }

        const bool isMoveKey = key == Offsets::eKI_W || key == Offsets::eKI_A ||
                               key == Offsets::eKI_S || key == Offsets::eKI_D;
        if (!isMoveKey) {
            return false;
        }
        if (key == Offsets::eKI_W && g_syntheticPressInFlight.load()) {
            return false; // our own press
        }
        if (event.state == Offsets::eIS_Pressed ||
            event.state == Offsets::eIS_Down) {
            g_manualInput.store(true);
        }
        return false; // never consume
    }

    bool OnInputEventUI(const void*) override { return false; }
    int GetPriority() const override { return 0; }
    bool _vf3(const void*) override { return false; }
};

InputListener s_inputListener;
bool s_listenerRegistered = false;

void EnsureInputListener()
{
    if (s_listenerRegistered) {
        return;
    }
    auto* env = SSystemGlobalEnvironment::GetInstance();
    if (env && env->pInput && env->pInput->AddEventListener(&s_inputListener)) {
        s_listenerRegistered = true;
        Log::Write("[AutoWalk] FollowController: input listener registered.");
    }
}

} // namespace

int g_steerInvert = 1;

Phase GetPhase()
{
    return g_phase.load();
}

void Enable()
{
    EnsureInputListener();
    g_phase.store(Phase::AwaitingRoad);
    g_state.flags |= 0x01; // console override engages directly
    g_state.flags |= 0x02;
    g_state.flags &= ~0x10;
    g_smoother = {};
    g_prevSmoothedYaw = 0.0f;
    Log::Write("[AutoWalk] FollowController: enabled (native-faithful).");
}

void Disable()
{
    ReleaseForward();
    g_state.flags = 0;
    g_phase.store(Phase::Disabled);
    Log::Write("[AutoWalk] FollowController: disabled.");
}

void Reset()
{
    ReleaseForward();
    g_state.flags = 0;
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
        g_state.flags = 0;
        g_phase.store(Phase::Disabled);
        Log::Write("[AutoWalk] FollowController: stopped (player unavailable or mounted).");
        return;
    }

    using namespace std::chrono;
    const auto now = steady_clock::now();
    float dt = 1.0f / 60.0f;
    if (g_lastTick != steady_clock::time_point{}) {
        dt = duration_cast<duration<float>>(now - g_lastTick).count();
        if (dt <= 0.0f) dt = 1.0f / 60.0f;
        if (dt > 0.25f) dt = 0.25f;
    }
    g_lastTick = now;

    // Native tuning values.
    const NativeMagnetism::FrameCVars* cvars = nullptr;
    if (!NativeMagnetism::RefreshFrameCVars(cvars) || !cvars) {
        Log::Write("[AutoWalk] FollowController: frame cvars unavailable.");
        Disable();
        return;
    }

    // Road acquisition through the native sampler (cached between samples).
    FootRoad::FootRoadProbe sample{};
    bool haveSample = false;
    if (g_lastSample == steady_clock::time_point{} ||
        now - g_lastSample >= milliseconds(static_cast<long long>(kSampleIntervalSeconds * 1000.0f))) {
        g_lastSample = now;
        haveSample = FootRoad::SampleRoadStandalone(kSampleDistance, sample);
        g_cachedSample = sample;
        g_cachedSampleValid = haveSample;
    } else if (g_cachedSampleValid) {
        haveSample = true;
        sample = g_cachedSample;
    }

    // State machine with the recovered semantics.
    const bool manual = g_manualInput.exchange(false);
    const bool engage = g_engageRequested.exchange(false);
    const bool failed = haveSample && sample.failed;
    const bool chat = IsChatFollowActive();

    const bool followActive = NativeMagnetism::TickStateMachine(
        g_state, dt, *cvars, manual, chat, failed, engage);

    if (!followActive || g_phase.load() == Phase::AwaitingRoad) {
        if (!followActive) {
            ReleaseForward();
            g_phase.store(Phase::Disabled);
            Log::Write("[AutoWalk] FollowController: deactivated (state machine).");
        }
        return;
    }

    // Steering: SmoothCD toward the native road yaw command.
    if (haveSample && sample.hasHit) {
        NativeMagnetism::SmoothCD(g_smoother, sample.yawFrom, dt, *cvars);

        // Apply the smoothed yaw delta through Henry's look accumulator —
        // the recovered mounted-camera seam (REL 37998 equivalent).
        float* accum = GetLookAccumulator();
        float playerYaw = 0.0f;
        if (accum && TryGetPlayerYaw(playerYaw)) {
            if (!g_smoother.initialized) {
                g_smoother.initialized = true;
                g_smoother.smoothed = playerYaw; // start aligned to the player
                g_prevSmoothedYaw = g_smoother.smoothed;
            }
            const float delta = g_smoother.smoothed - g_prevSmoothedYaw;
            g_prevSmoothedYaw = g_smoother.smoothed;
            const float sign = g_steerInvert ? -1.0f : 1.0f;
            accum[2] += delta * sign; // Ang3 z = yaw
        }

        HoldForward();
        g_phase.store(Phase::Following);
    } else {
        ReleaseForward();
        g_phase.store(Phase::AwaitingRoad);
    }

    if (g_lastLog == steady_clock::time_point{} ||
        now - g_lastLog >= seconds(2)) {
        g_lastLog = now;
        Log::Write(std::string("[AutoWalk] follow: flags=") +
                   std::to_string(g_state.flags) +
                   " hasHit=" + std::to_string(haveSample && sample.hasHit) +
                   " yawFrom=" + std::to_string(sample.yawFrom) +
                   " smoothed=" + std::to_string(g_smoother.smoothed) +
                   " deact=" + std::to_string(g_state.deactivateTime) +
                   " react=" + std::to_string(g_state.reactivateTime));
    }
}

} // namespace AutoWalk::FollowController
