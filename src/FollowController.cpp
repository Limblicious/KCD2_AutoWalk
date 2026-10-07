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
#include "game/C_CameraManager.h"
#include "game/S_GameContext.h"
#include "playermodule/C_PlayerModule.h"
#include "playermodule/I_ActionSets.h"

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
std::atomic<bool> g_disengageRequested{false};
std::atomic<bool> g_manualInput{false};
float g_manualHeldTime = 0.0f;
constexpr float kManualHoldDeactivateSeconds = 3.0f;

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

// Henry's C_ActorPhysicsState (C_Actor+0x238). The on-foot first-person
// camera compose builds the view from the physics-state look angles; the
// entity world TM (entity+0x58) is the BODY the movement consumes. Steering
// rotates the body only, leaving the camera on the look state (mounted-style
// decoupling).
float* GetLookAngles()
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
    return reinterpret_cast<float*>(reinterpret_cast<std::uintptr_t>(state) + 0x08);
}

// Entity world TM (Matrix34: right@0..2, tx@3, forward@4..6, ty@7, up@8..10, tz@11).
float* GetEntityWorldTM()
{
    auto* framework = CCryAction::GetInstance();
    auto* entity = framework ? framework->GetClientEntity() : nullptr;
    if (!entity) {
        return nullptr;
    }
    return reinterpret_cast<float*>(reinterpret_cast<std::uintptr_t>(entity) + 0x58);
}

// Rotates the entity world TM around world Z (yaw) by 'theta' radians.
void RotateEntityYaw(float theta)
{
    float* tm = GetEntityWorldTM();
    if (!tm) {
        return;
    }
    const float c = std::cos(theta);
    const float s = std::sin(theta);
    const auto rot = [&](float x, float y) {
        return std::pair<float, float>(x * c - y * s, x * s + y * c);
    };
    auto r = rot(tm[0], tm[1]);
    tm[0] = r.first;
    tm[1] = r.second;
    auto f = rot(tm[4], tm[5]);
    tm[4] = f.first;
    tm[5] = f.second;
}

// User-held W tracking: the listener sees fresh (untagged) W presses and
// releases; the synthetic hold is tagged and ignored.
std::atomic<bool> g_userWHeld{false};

bool IsManualHeld()
{
    // Never synthesized by the plugin: a held A/S/D in the input queue is
    // always the player. W is tracked through the listener above.
    return g_userWHeld.load() ||
           InputSeam::IsKeyHeld(Offsets::eKI_A) ||
           InputSeam::IsKeyHeld(Offsets::eKI_S) ||
           InputSeam::IsKeyHeld(Offsets::eKI_D);
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
        // Engagement is handled by the native hold-E prompt actions
        // (foot_magnetism_activate/deactivate); the listener only tracks
        // manual movement keys.
        const bool isMoveKey = key == Offsets::eKI_W || key == Offsets::eKI_A ||
                               key == Offsets::eKI_S || key == Offsets::eKI_D;
        if (!isMoveKey) {
            return false;
        }
        if (key == Offsets::eKI_W && g_syntheticPressInFlight.load()) {
            return false; // our own press
        }
        // Only fresh presses count as manual input: the engine re-broadcasts
        // held keys as Down events every frame, which must not be mistaken
        // for player input while we hold W synthetically.
        if (event.state == Offsets::eIS_Pressed) {
            g_manualInput.store(true);
            if (key == Offsets::eKI_W) {
                g_userWHeld.store(true);
            }
        } else if (event.state == Offsets::eIS_Released && key == Offsets::eKI_W) {
            g_userWHeld.store(false);
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

wh::playermodule::I_ActionSets* GetActionSets()
{
    auto* ctx = wh::game::S_GameContext::GetInstance();
    if (!ctx) {
        return nullptr;
    }
    const auto playerModule = *reinterpret_cast<void**>(
        reinterpret_cast<std::uintptr_t>(ctx) + 0x128);
    if (!playerModule) {
        return nullptr;
    }
    return *reinterpret_cast<wh::playermodule::I_ActionSets**>(
        reinterpret_cast<std::uintptr_t>(playerModule) + 0x60);
}

} // namespace

int g_steerInvert = 1;

Phase GetPhase()
{
    return g_phase.load();
}

void RegisterPromptActions()
{
    auto* actionSets = GetActionSets();
    if (!actionSets) {
        Log::Write("[AutoWalk] FollowController: I_ActionSets unavailable.");
        return;
    }

    // The context must be the ACTIONMAP name the rows live in (defaultProfile/
    // defaultActionHelp "player"), otherwise the registry lookup misses and
    // the bind is silently skipped.
    CryStringT<char> ctx("player");
    CryStringT<char> activate("foot_magnetism_activate");
    CryStringT<char> deactivate("foot_magnetism_deactivate");

    actionSets->RegisterAction(
        ctx, activate,
        std::function<void()>([]() { FollowController::RequestEngage(); }), 1);
    actionSets->RegisterAction(
        ctx, deactivate,
        std::function<void()>([]() { FollowController::RequestDisengage(); }), 1);
    const bool regA = actionSets->IsRegistered(ctx, activate);
    const bool regD = actionSets->IsRegistered(ctx, deactivate);
    Log::Write(std::string("[AutoWalk] FollowController: prompt actions registered (rows present: activate=") +
               std::to_string(regA) + " deactivate=" + std::to_string(regD) + ").");
}

// Mirror of the native hint updater (S_AutoController prompt tick, the
// horse_mounted analog): disable-reason + enabled + visible per row.
void UpdatePromptFlags(bool onRoad, bool engaged, bool manualHeld)
{
    auto* actionSets = GetActionSets();
    if (!actionSets) {
        return;
    }

    const bool chatFollow = IsChatFollowActive();
    const bool showActivate = onRoad && !engaged && !chatFollow && !manualHeld;
    const bool showDeactivate = engaged && !chatFollow && !manualHeld;

    static int lastShowA = -1;
    static int lastShowD = -1;
    if (lastShowA != showActivate || lastShowD != showDeactivate) {
        lastShowA = showActivate;
        lastShowD = showDeactivate;
        Log::Write(std::string("[AutoWalk] prompt flags: activate=") +
                   std::to_string(showActivate) +
                   " deactivate=" + std::to_string(showDeactivate) +
                   " onRoad=" + std::to_string(onRoad) +
                   " engaged=" + std::to_string(engaged) +
                   " manual=" + std::to_string(manualHeld) +
                   " chat=" + std::to_string(chatFollow));
    }

    CryStringT<char> ctx("player");
    CryStringT<char> activate("foot_magnetism_activate");
    CryStringT<char> deactivate("foot_magnetism_deactivate");

    // [7] disable reason on the activate row (by value, callee destroys).
    CryStringT<char> reason(chatFollow ? "ui_magnetism_in_follow"
                                       : "ui_magnetism_not_on_path");
    actionSets->SetActionDisableReason(ctx, activate, reason, 1);

    // [4] enabled state.
    actionSets->SetActionEnabled(ctx, activate, showActivate, 1);
    actionSets->SetActionEnabled(ctx, deactivate, showDeactivate, 1);

    // [5] visible state.
    actionSets->SetActionVisible(ctx, activate, showActivate, 1);
    actionSets->SetActionVisible(ctx, deactivate, showDeactivate, 1);
}

void RequestEngage()
{
    g_engageRequested.store(true);
}

void RequestDisengage()
{
    g_disengageRequested.store(true);
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
    UpdatePromptFlags(false, false, false);
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
    auto* framework = CCryAction::GetInstance();
    auto* player = framework
        ? static_cast<wh::entitymodule::C_Player*>(framework->GetClientActor())
        : nullptr;

    if (!player || IsMounted()) {
        if (g_phase.load() != Phase::Disabled) {
            ReleaseForward();
            g_state.flags = 0;
            g_phase.store(Phase::Disabled);
            Log::Write("[AutoWalk] FollowController: stopped (player unavailable or mounted).");
        }
        UpdatePromptFlags(false, false, false);
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

    // Idle prompt: show "hold E to follow path" whenever the player is on
    // foot, on a road, and not following -- no console command involved.
    const bool engaged = (g_state.flags & 0x01) != 0;
    const bool onRoad = haveSample && sample.hasHit;

    if (g_phase.load() == Phase::Disabled && !engaged) {
        UpdatePromptFlags(onRoad, false, false);
    }

    // Engagement via the native hold-E dispatch.
    const bool engage = g_engageRequested.exchange(false);
    const bool disengage = g_disengageRequested.exchange(false);
    if (engage && !engaged && onRoad) {
        g_state.flags |= 0x01;
        g_state.flags |= 0x02;
        g_state.flags &= ~0x10;
        g_smoother = {};
        g_prevSmoothedYaw = 0.0f;
        g_phase.store(Phase::AwaitingRoad);
        Log::Write("[AutoWalk] FollowController: engaged via hold E.");
    }
    if (disengage) {
        Disable();
        return;
    }
    if (!engaged) {
        return;
    }

    // State machine with the recovered semantics.
    const bool manual = g_manualInput.exchange(false);
    const bool failed = haveSample && sample.failed;
    const bool chat = IsChatFollowActive();

    // Manual WASD: camera re-couples to travel; held >3s deactivates.
    const bool manualHeld = IsManualHeld();
    if (manualHeld) {
        g_manualHeldTime += dt;
        if (g_manualHeldTime >= kManualHoldDeactivateSeconds) {
            Log::Write("[AutoWalk] FollowController: deactivated after 3s of manual input.");
            Disable();
            return;
        }
    } else {
        g_manualHeldTime = 0.0f;
    }

    const bool followActive = NativeMagnetism::TickStateMachine(
        g_state, dt, *cvars, manual, chat, failed, false);

    if (!followActive) {
        ReleaseForward();
        g_phase.store(Phase::Disabled);
        UpdatePromptFlags(false, false, false);
        Log::Write("[AutoWalk] FollowController: deactivated (state machine).");
        return;
    }

    const bool following = onRoad;
    UpdatePromptFlags(following, true, manualHeld);

    // Steering: SmoothCD toward the native road yaw command; rotate the BODY
    // (entity world TM) so the camera stays decoupled like on horseback.
    if (following && !manualHeld) {
        NativeMagnetism::SmoothCD(g_smoother, sample.yawFrom, dt, *cvars);

        float playerYaw = 0.0f;
        if (TryGetPlayerYaw(playerYaw)) {
            if (!g_smoother.initialized) {
                g_smoother.initialized = true;
                g_smoother.smoothed = playerYaw; // start aligned to the player
                g_prevSmoothedYaw = g_smoother.smoothed;
            }
            const float delta = g_smoother.smoothed - g_prevSmoothedYaw;
            g_prevSmoothedYaw = g_smoother.smoothed;
            const float sign = g_steerInvert ? -1.0f : 1.0f;
            RotateEntityYaw(delta * sign);
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
