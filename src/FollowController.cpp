#include "FollowController.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>

#include <MinHook.h>

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

// --- Movement-controller hook (camera-decoupled body steering) -------------
// The recovered on-foot body-turn seam: C_ActorMovementController (actor
// +0x180) vf13 (0x1804B8E88) fills the per-frame S_MountAnimState request;
// its m_desiredVelocity (+0x0C) is the world-rotated walk direction. The
// hook rotates that velocity by the follow yaw delta: Henry's body turns
// through the movement system while the look state (the camera) is never
// touched -- the mounted decoupling.
using MovementRequestFn = void (*)(void* self, float dt, float* out);

MovementRequestFn g_originalMovementRequest = nullptr;
bool g_movementHookInstalled = false;
bool g_movementHookAttempted = false;
float g_pendingYawDelta = 0.0f;
int g_hookMisses = 0;

void MovementRequestHook(void* self, float dt, float* out)
{
    g_originalMovementRequest(self, dt, out);

    const float d = g_pendingYawDelta;
    g_pendingYawDelta = 0.0f;
    if (d == 0.0f) {
        return;
    }
    // m_desiredVelocity at out+0x0C (floats [3],[4],[5]); yaw-rotate in the
    // world XY plane.
    const float c = std::cos(d);
    const float s = std::sin(d);
    const float x = out[3];
    const float y = out[4];
    out[3] = x * c - y * s;
    out[4] = x * s + y * c;
}

void EnsureMovementHook()
{
    if (g_movementHookInstalled || g_movementHookAttempted) {
        return;
    }
    g_movementHookAttempted = true;

    // The runtime vtable entries of C_ActorMovementController are
    // interfuscator-rewritten to non-executable trampolines, so the hook
    // targets the real function at its static image address: vf13
    // 0x1804B8E88 (RVA 0x4B8E88; pinned libKCD2 offset, verified in Ghidra).
    const auto moduleBase = reinterpret_cast<std::uintptr_t>(
        GetModuleHandleA("WHGame.dll"));
    if (!moduleBase) {
        return;
    }
    void* target = reinterpret_cast<void*>(moduleBase + 0x4B8E88);
    // Diagnostics: the game's interfuscator patches function prologues at
    // startup; a patched prologue makes MinHook's disassembler fail.
    const auto* bytes = static_cast<const unsigned char*>(target);
    char prologue[64];
    std::snprintf(prologue, sizeof(prologue),
                  "%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
                  bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5],
                  bytes[6], bytes[7], bytes[8], bytes[9], bytes[10], bytes[11]);
    const MH_STATUS status = MH_CreateHook(
        target, reinterpret_cast<void*>(&MovementRequestHook),
        reinterpret_cast<void**>(&g_originalMovementRequest));
    if (status != MH_OK) {
        Log::Write(std::string("[AutoWalk] FollowController: movement hook create failed (status=") +
                   std::to_string(static_cast<int>(status)) +
                   " prologue=" + prologue + ").");
        return;
    }
    if (MH_EnableHook(target) != MH_OK) {
        Log::Write("[AutoWalk] FollowController: movement hook enable failed.");
        return;
    }
    g_movementHookInstalled = true;
    Log::Write("[AutoWalk] FollowController: movement-controller hook installed.");
}

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

    // Following: the native latch owns the decision. During a brief sample
    // miss the latch survives (grace window) and the last yaw command
    // persists -- Henry keeps curving back onto the road.
    const bool following = latched;
    UpdatePromptFlags(onRoad, latched, manualHeld);

    // Steering: SmoothCD chases the native road yaw command; the per-frame
    // delta of the smoothed yaw rotates the movement request's desired
    // velocity (the movement-controller hook) -- Henry's body turns through
    // the movement system while the look state (the camera) is untouched:
    // the mounted decoupling.
    if (following && !manualHeld) {
        // Sample-acceptance gate recovered from SetHoldLatchedImpl (gate 5):
        // a sample is only accepted within RoadMagnetismEnterAngle of the
        // current command. The standalone facade flips between road branches
        // at crossroads (a 100+ degree command jump); the gate rejects the
        // flips while passing slow legitimate curves -- the native
        // path-continuity behavior.
        const float rawTarget = FootRoad::NativeMagnetYaw();
        if (!g_targetValid) {
            g_targetYaw = rawTarget;
            g_targetValid = true;
        } else if (std::abs(WrapPi(rawTarget - g_targetYaw)) * 57.2957795f <=
                   cvars->enterAngle) {
            g_targetYaw = rawTarget;
        }

        EnsureMovementHook();
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

        if (g_movementHookInstalled) {
            // Did the hook consume the previous frame's delta? (it zeroes
            // the shared value when the movement request runs). If not, the
            // interfuscated vtable path bypasses the static body -- fall
            // back to the proven look-accum channel.
            if (g_pendingYawDelta != 0.0f) {
                g_hookMisses++;
            } else {
                g_hookMisses = 0;
            }
            if (g_hookMisses >= 3 && delta != 0.0f) {
                if (auto* state = GetPhysicsState()) {
                    state->m_lookAngleAccum.z += delta;
                }
            }
            g_pendingYawDelta = delta;

            // Camera decoupling (first person): the on-foot look-body
            // coupling rotates the view with the body's turn; counter-rotate
            // the look state by the commanded delta so the view stays put
            // while the body walks the road. The mouse still adds its own
            // look on top (untouched request channel).
            if (g_hookMisses < 3 && delta != 0.0f) {
                if (auto* state = GetPhysicsState()) {
                    state->m_lookAngles.z -= delta;
                }
            }
        } else {
            if (auto* state = GetPhysicsState()) {
                state->m_lookAngleAccum.z += delta;
            }
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
                   " target=" + std::to_string(g_targetYaw) +
                   " yawFrom=" + std::to_string(sample.yawFrom) +
                   " along=(" + std::to_string(sample.alongX) + "," +
                   std::to_string(sample.alongY) + ")" +
                   " player=(" + std::to_string(sample.playerX) + "," +
                   std::to_string(sample.playerY) + ")" +
                   " smoothed=" + std::to_string(g_smoother.smoothed) +
                   " rotMax=" + std::to_string(cvars->rotationMax) +
                   " enterAngle=" + std::to_string(cvars->enterAngle) +
                   " velDelta=" + std::to_string(g_pendingYawDelta));
    }
}

} // namespace AutoWalk::FollowController
