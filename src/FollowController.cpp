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
#include "entitymodule/C_ActorPhysicsState.h"
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

std::chrono::steady_clock::time_point g_lastLog{};
std::chrono::steady_clock::time_point g_lastTick{};

NativeMagnetism::YawSmoother g_smoother{};
float g_prevSmoothedYaw = 0.0f;
float g_targetYaw = 0.0f;
bool g_targetValid = false;
bool g_nativeEnsured = false;

// User-input signals gathered by the input listener each frame.
std::atomic<bool> g_engageRequested{false};
std::atomic<bool> g_disengageRequested{false};
std::atomic<bool> g_manualInput{false};
float g_manualHeldTime = 0.0f;
constexpr float kManualHoldDeactivateSeconds = 3.0f;

// True while the current PostInputEvent call is our own synthetic W press.
std::atomic<bool> g_syntheticPressInFlight{false};

float WrapPi(float a)
{
    while (a > 3.14159265f) a -= 6.28318531f;
    while (a < -3.14159265f) a += 6.28318531f;
    return a;
}

// Henry's C_ActorPhysicsState (C_Actor+0x238): the vanilla look-state machine
// behind the view. The mounted seam (HorseFlatYaw_To_RiderLookAccum,
// REL 37998) pumps the horse yaw delta into its +0x88 m_lookAngleAccum; the
// foot port pumps the smoothed road-yaw delta into the same channel.
wh::entitymodule::C_ActorPhysicsState* GetPhysicsState()
{
    auto* framework = CCryAction::GetInstance();
    auto* player = framework
        ? static_cast<wh::entitymodule::C_Player*>(framework->GetClientActor())
        : nullptr;
    if (!player) {
        return nullptr;
    }
    return *reinterpret_cast<wh::entitymodule::C_ActorPhysicsState**>(
        reinterpret_cast<std::uintptr_t>(player) + 0x238);
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
    g_smoother = {};
    g_prevSmoothedYaw = 0.0f;
    g_targetYaw = 0.0f;
    g_targetValid = false;
    FootRoad::NativeSetHoldLatched(true);
    Log::Write("[AutoWalk] FollowController: enabled (native latch).");
}

void Disable()
{
    ReleaseForward();
    FootRoad::NativeSetHoldLatched(false);
    g_phase.store(Phase::Disabled);
    g_targetValid = false;
    UpdatePromptFlags(false, false, false);
    Log::Write("[AutoWalk] FollowController: disabled.");
}

void Reset()
{
    ReleaseForward();
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
            FootRoad::NativeSetHoldLatched(false);
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

    // One-time native facade wiring: property vtable (game GetValue), the
    // rider-sync fabrication (GetRider -> real player) and the neutral move
    // adapter. The native rebuild then creates the on-press controller.
    if (!g_nativeEnsured) {
        g_nativeEnsured = true;
        FootRoad::EnsureNativeRoadFollow();
        Log::Write(std::string("[AutoWalk] FollowController: native road-follow initialized, ready=") +
                   std::to_string(FootRoad::NativeFollowReady()));
    }

    // The full native tick (S_HorseRoadFollow_Tick REL 56405): sampling,
    // path persistence, controller phases and the yaw command -- the same
    // chain S_HorseData_Update runs for the mounted horse.
    FootRoad::FootRoadProbe sample{};
    const bool haveSample = FootRoad::TickNativeRoadFollow(dt, sample);

    const bool onRoad = haveSample && sample.hasHit;
    const bool latched = FootRoad::NativeLatched();

    // Engagement via the native hold-E dispatch: the OnPress SetHoldLatched
    // is a bit-trivial latch; the tick's own phases decide enter/reject.
    const bool engage = g_engageRequested.exchange(false);
    const bool disengage = g_disengageRequested.exchange(false);
    if (engage && !latched && onRoad) {
        FootRoad::NativeSetHoldLatched(true);
        Log::Write("[AutoWalk] FollowController: latched via hold E.");
    }
    if (disengage) {
        FootRoad::NativeSetHoldLatched(false);
        Disable();
        return;
    }

    // Manual WASD: held >3s deactivates (user requirement; the native
    // phases already handle the graceful interruption/falloff).
    const bool manualHeld = IsManualHeld();
    if (manualHeld) {
        g_manualHeldTime += dt;
        if (g_manualHeldTime >= kManualHoldDeactivateSeconds) {
            Log::Write("[AutoWalk] FollowController: deactivated after 3s of manual input.");
            FootRoad::NativeSetHoldLatched(false);
            Disable();
            return;
        }
    } else {
        g_manualHeldTime = 0.0f;
    }

    if (!latched) {
        if (g_phase.load() != Phase::Disabled) {
            ReleaseForward();
            g_phase.store(Phase::Disabled);
            Log::Write("[AutoWalk] FollowController: native follow released.");
        }
        UpdatePromptFlags(onRoad, false, false);
        return;
    }

    // Following: the native state owns the decision; this side mirrors it.
    const bool following = onRoad && FootRoad::NativeMagnetismLive();
    UpdatePromptFlags(onRoad, latched, manualHeld);

    // Steering: the vanilla mounted seam (HorseFlatYaw_To_RiderLookAccum,
    // 0x1806CCAF8 / REL 37998). SmoothCD chases the native road yaw command
    // (m_magnetYaw); the per-frame delta of the smoothed yaw is pumped into
    // Henry's m_lookAngleAccum (C_ActorPhysicsState+0x88). The physics-state
    // tick folds it into m_lookAngles, the first-person compose reads
    // m_viewRotation, and the on-foot body follows the view -- exactly the
    // mounted pipeline, with the mouse untouched on the request channel.
    if (following && !manualHeld) {
        // The native sample command is accepted directly while following:
        // the recovered enter-angle gate (SetHoldLatchedImpl gate 5) applies
        // only when NOT latched (engagement); while latched the samples flow.
        const float target = FootRoad::NativeMagnetYaw();
        g_targetYaw = target;
        g_targetValid = true;

        NativeMagnetism::SmoothCD(g_smoother, g_targetYaw, dt, *cvars);

        if (!g_smoother.initialized) {
            g_smoother.initialized = true;
            if (auto* state = GetPhysicsState()) {
                g_smoother.smoothed = state->m_lookAngles.z; // seed at current view yaw
            }
            g_prevSmoothedYaw = g_smoother.smoothed;
        }

        const float sign = g_steerInvert ? -1.0f : 1.0f;
        const float delta = WrapPi(g_smoother.smoothed - g_prevSmoothedYaw) * sign;
        g_prevSmoothedYaw = g_smoother.smoothed;

        if (auto* state = GetPhysicsState()) {
            state->m_lookAngleAccum.z += delta;
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
        Log::Write(std::string("[AutoWalk] follow: latched=") +
                   std::to_string(latched) +
                   " hasHit=" + std::to_string(onRoad) +
                   " live=" + std::to_string(FootRoad::NativeMagnetismLive()) +
                   " magnetYaw=" + std::to_string(FootRoad::NativeMagnetYaw()) +
                   " smoothed=" + std::to_string(g_smoother.smoothed) +
                   " rotMax=" + std::to_string(cvars->rotationMax) +
                   " accum=" + std::to_string(GetPhysicsState() ? GetPhysicsState()->m_lookAngleAccum.z : 0.0f));
    }
}

} // namespace AutoWalk::FollowController
