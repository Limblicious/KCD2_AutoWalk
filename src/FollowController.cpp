#include "FollowController.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>

#include <MinHook.h>

#include "FootRoad.h"
#include "FollowInteractionState.h"
#include "HorseCameraRecenter.h"
#include "InputSeam.h"
#include "Log.h"
#include "NativeMagnetism.h"
#include "RoadFollowPort.h"
#include "Offsets/vtables/IInput.h"
#include "Offsets/vtables/IInputEventListener.h"
#include "crysystem/CCamera.h"
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

// The autonomous travel frame: a body/travel heading independent of the
// camera look. The native steering command turns this frame; the movement
// hook replaces the vanilla camera-relative velocity direction with it.
std::atomic<bool> g_followActive{false};
std::atomic<bool> g_cameraRecenteringEnabled{false};
float g_travelYaw = 0.0f;
bool g_travelValid = false;

wh::entitymodule::C_Player* GetPlayer()
{
    auto* framework = CCryAction::GetInstance();
    return framework
        ? static_cast<wh::entitymodule::C_Player*>(framework->GetClientActor())
        : nullptr;
}

float g_lastPosX = 0.0f;
float g_lastPosY = 0.0f;
float g_lastPosZ = 0.0f;
bool g_lastPosValid = false;

// Rolling position history (~1 s at 60 Hz) for the ground-relative
// displacement heading over ~0.5 s, immune to per-frame noise.
constexpr int kPosWindowSize = 60;
constexpr int kPosWindowSpan = 30;
float g_posWinX[kPosWindowSize] = {};
float g_posWinY[kPosWindowSize] = {};
int g_posWinCount = 0;
int g_posWinHead = 0;

void ResetPositionHistory()
{
    g_lastPosValid = false;
    g_posWinCount = 0;
    g_posWinHead = 0;
}

// The engine's current view camera matrix (CSystem+0x288 CCamera): the
// ACTUAL displayed camera orientation, independent of the view state.
float ReadCameraYaw()
{
    auto* env = SSystemGlobalEnvironment::GetInstance();
    if (!env || !env->_unkC8) {
        return 0.0f;
    }
    const auto* cam = reinterpret_cast<const CCamera*>(
        reinterpret_cast<std::uintptr_t>(env->_unkC8) + 0x288);
    const Vec3 fwd = cam->GetViewdir();
    return std::atan2(-fwd.x, fwd.y);
}

void MovementRequestHook(void* self, float dt, float* out)
{
    g_originalMovementRequest(self, dt, out);
    auto* player = GetPlayer();
    if (!player || self != player->m_pMovementController) {
        return;
    }
    if (!g_followActive.load() || !g_travelValid) {
        return;
    }
    // Keep the vanilla speed magnitude; replace only the horizontal
    // direction with the autonomous travel heading. CryEngine yaw
    // convention (Quat::GetRotZ = atan2(-fwdX, fwdY)): forward = (-sin z,
    // cos z).
    const float x = out[3];
    const float y = out[4];
    const float speed = std::sqrt(x * x + y * y);
    const float yaw = g_travelYaw;
    out[3] = -std::sin(yaw) * speed;
    out[4] = std::cos(yaw) * speed;
    if (g_cameraRecenteringEnabled.load()) {
        // Reference-frame ground truth for the double-rotation experiment.
        // Diagnostics only: nothing here feeds the camera or movement math.
        HorseCameraRecenter::GroundTruth gt;
        gt.travelYaw = yaw;
        gt.requestYaw = yaw; // the override sets the request exactly to travel
        gt.vanillaRequestYaw = std::atan2(-x, y);
        gt.dt = dt;
        gt.entityYaw = yaw;
        auto* framework = CCryAction::GetInstance();
        if (auto* entity = framework ? framework->GetClientEntity() : nullptr) {
            const auto* tm = reinterpret_cast<const float*>(
                reinterpret_cast<std::uintptr_t>(entity) + 0x58);
            gt.entityYaw = std::atan2(-tm[1], tm[5]);
            const float px = tm[3];
            const float py = tm[7];
            const float pz = tm[11];
            if (g_lastPosValid) {
                const float dx = px - g_lastPosX;
                const float dy = py - g_lastPosY;
                const float distSq = dx * dx + dy * dy;
                gt.dx = dx;
                gt.dy = dy;
                // A frame step below the noise floor cannot be measured;
                // a step above 3 m is a teleport/load, not travel.
                if (distSq > 1.0e-6f && distSq < 9.0f) {
                    gt.moveYaw = std::atan2(-dx, dy);
                    gt.moveValid = true;
                }
            }
            g_lastPosX = px;
            g_lastPosY = py;
            g_lastPosZ = pz;
            g_lastPosValid = true;

            g_posWinX[g_posWinHead] = px;
            g_posWinY[g_posWinHead] = py;
            g_posWinHead = (g_posWinHead + 1) % kPosWindowSize;
            if (g_posWinCount < kPosWindowSize) {
                ++g_posWinCount;
            }
            if (g_posWinCount >= kPosWindowSpan) {
                const int oldest =
                    (g_posWinHead + kPosWindowSize - kPosWindowSpan) %
                    kPosWindowSize;
                const float wx = px - g_posWinX[oldest];
                const float wy = py - g_posWinY[oldest];
                if (wx * wx + wy * wy > 1.0e-4f) {
                    gt.windowYaw = std::atan2(-wx, wy);
                    gt.windowValid = true;
                }
            }
        }
        if (player->m_pPhysicsState) {
            gt.flatYaw = player->m_pPhysicsState->m_flatYaw;
        }
        gt.cameraYaw = ReadCameraYaw();
        // S_MountAnimState+0x18 is the Ang3 look request copied into
        // C_ActorPhysicsState::m_lookDeltaRequest by the immediately
        // following native physics-state tick.
        HorseCameraRecenter::Update(player, dt, out[6], out[8], gt);
    }
    // NOTE: m_rootRotation (out+0x3C) is NOT overwritten here -- its exact
    // role is still "MED"-unverified (quat vs look/aim data), and writing it
    // was observed to couple the mouse look into the travel direction. The
    // body-facing seam stays pending Track A recovery.
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
float g_targetYaw = 0.0f;
bool g_targetValid = false;
bool g_nativeEnsured = false;

// Central travel-state cleanup: every disengagement path (explicit Disable,
// Reset, mounted/unavailable, the machine's release, manual relinquish) must
// clear ALL of these, otherwise MovementRequestHook keeps forcing the old
// travel yaw after the follow has ended.
void ResetTravelFrame()
{
    g_followActive.store(false);
    g_cameraRecenteringEnabled.store(false);
    HorseCameraRecenter::Reset();
    g_travelValid = false;
    g_targetValid = false;
    g_smoother = {};
    // The position history is only valid within one continuous follow
    // session; stale deltas across a teleport/load would otherwise be
    // reported as travel.
    ResetPositionHistory();
}

FollowInteraction::ToggleRequest g_toggleRequest;

float WrapPi(float a)
{
    while (a > 3.14159265f) a -= 6.28318531f;
    while (a < -3.14159265f) a += 6.28318531f;
    return a;
}

bool IsPhysicalWDown()
{
    // Engine-synthesized input does not affect the OS key state, so this
    // remains unambiguous while synthetic W owns the engine's held queue.
    return (GetAsyncKeyState('W') & 0x8000) != 0;
}

bool IsManualHeld()
{
    const auto w = FollowInteraction::ResolveManualW(
        g_wHeld, InputSeam::IsKeyHeld(Offsets::eKI_W), IsPhysicalWDown());

    // Never synthesized by the plugin: a held A/S/D in the input queue is
    // always the player.
    return w.manualHeld ||
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
    const auto w = FollowInteraction::ResolveManualW(
        g_wHeld, InputSeam::IsKeyHeld(Offsets::eKI_W), IsPhysicalWDown());
    if (w.releaseSynthetic) {
        InputSeam::PostKeyEvent(Offsets::eKI_W, Offsets::eIS_Released, 0.0f);
    }
    g_wHeld = false;
}

void HoldForward()
{
    if (!InputSeam::IsKeyHeld(Offsets::eKI_W)) {
        InputSeam::PostKeyEvent(Offsets::eKI_W, Offsets::eIS_Pressed, 1.0f);
    }
    g_wHeld = true;
}


// Target the actual KCD2 UI lifecycle rather than treating arbitrary
// framework pause-source counters as "menu open":
//   - C_UIMenu::Open/Close = the ESC/root pause menu.
//   - C_UIFullUIModeHelper::OnSourceEvent = the full-UI mode (inventory,
//     perks, map, quest log, alchemy, etc.) -- the game's source monitor
//     broadcasts the active/inactive transitions to it.
//   - C_UISkiptime::ShowDialog/HideDialog/RemoveDialog = the T wait screen.
std::atomic<bool> g_rootMenuOpen{false};
std::atomic<bool> g_fullUIModeActive{false};
std::atomic<bool> g_skipTimeDialogOpen{false};
bool g_menuHooksAttempted = false;
bool g_menuHooksInstalled = false;

bool IsInputOwningUIOpen()
{
    return g_rootMenuOpen.load() || g_fullUIModeActive.load() ||
           g_skipTimeDialogOpen.load();
}

using UIMenuOpenFn = void (*)(void* self, char mode);
using UIMenuCloseFn = void (*)(void* self);
using FullUIModeEventFn = void (*)(void* self, void* a2, bool bActive);
using SkipTimeDialogFn = void (*)(void* self);
UIMenuOpenFn g_originalUIMenuOpen = nullptr;
UIMenuCloseFn g_originalUIMenuClose = nullptr;
FullUIModeEventFn g_originalFullUIModeEvent = nullptr;
SkipTimeDialogFn g_originalSkipTimeShowDialog = nullptr;
SkipTimeDialogFn g_originalSkipTimeHideDialog = nullptr;
SkipTimeDialogFn g_originalSkipTimeRemoveDialog = nullptr;

void UIMenuOpenHook(void* self, char mode)
{
    // Release before the menu begins consuming keyboard input so our synthetic
    // W cannot become a held navigation key.
    g_toggleRequest.Block(FollowInteraction::ToggleBlocker::RootMenu);
    g_rootMenuOpen.store(true);
    g_cameraRecenteringEnabled.store(false);
    HorseCameraRecenter::Reset();
    ReleaseForward();
    g_lastTick = {};
    if (g_originalUIMenuOpen) {
        g_originalUIMenuOpen(self, mode);
    }
}

void UIMenuCloseHook(void* self)
{
    if (g_originalUIMenuClose) {
        g_originalUIMenuClose(self);
    }
    // Do not synthesize W from the UI hook. The normal follow tick will resume
    // it on the next frame if the mode-1 road latch is still active.
    g_rootMenuOpen.store(false);
    g_toggleRequest.Unblock(FollowInteraction::ToggleBlocker::RootMenu);
    g_lastTick = {};
}

void FullUIModeEventHook(void* self, void* a2, bool bActive)
{
    g_fullUIModeActive.store(bActive);
    if (bActive) {
        g_toggleRequest.Block(FollowInteraction::ToggleBlocker::FullUI);
        g_cameraRecenteringEnabled.store(false);
        HorseCameraRecenter::Reset();
        ReleaseForward();
        g_lastTick = {};
    }
    if (g_originalFullUIModeEvent) {
        g_originalFullUIModeEvent(self, a2, bActive);
    }
    if (!bActive) {
        g_toggleRequest.Unblock(FollowInteraction::ToggleBlocker::FullUI);
    }
}

void SkipTimeShowDialogHook(void* self)
{
    // Release before SkipTime.gfx starts consuming keyboard input.
    g_toggleRequest.Block(FollowInteraction::ToggleBlocker::SkipTime);
    const bool wasOpen = g_skipTimeDialogOpen.exchange(true);
    g_cameraRecenteringEnabled.store(false);
    HorseCameraRecenter::Reset();
    ReleaseForward();
    g_lastTick = {};
    if (!wasOpen) {
        Log::Write("[AutoWalk] FollowController: skip-time dialog opened; released synthetic W.");
    }
    if (g_originalSkipTimeShowDialog) {
        g_originalSkipTimeShowDialog(self);
    }
}

void SkipTimeHideDialogHook(void* self)
{
    if (g_originalSkipTimeHideDialog) {
        g_originalSkipTimeHideDialog(self);
    }
    const bool wasOpen = g_skipTimeDialogOpen.exchange(false);
    g_toggleRequest.Unblock(FollowInteraction::ToggleBlocker::SkipTime);
    g_lastTick = {};
    if (wasOpen) {
        Log::Write("[AutoWalk] FollowController: skip-time dialog closed.");
    }
}

void SkipTimeRemoveDialogHook(void* self)
{
    if (g_originalSkipTimeRemoveDialog) {
        g_originalSkipTimeRemoveDialog(self);
    }
    const bool wasOpen = g_skipTimeDialogOpen.exchange(false);
    g_toggleRequest.Unblock(FollowInteraction::ToggleBlocker::SkipTime);
    g_lastTick = {};
    if (wasOpen) {
        Log::Write("[AutoWalk] FollowController: skip-time dialog removed.");
    }
}

void EnsureMenuHooks()
{
    if (g_menuHooksInstalled || g_menuHooksAttempted) {
        return;
    }
    g_menuHooksAttempted = true;

    const auto moduleBase = reinterpret_cast<std::uintptr_t>(
        GetModuleHandleA("WHGame.dll"));
    if (!moduleBase) {
        return;
    }

    // wh::I_UIMenu::Open / Close implementations for KCD2 1.5.6.
    void* openTarget = reinterpret_cast<void*>(moduleBase + 0xC04B38);
    void* closeTarget = reinterpret_cast<void*>(moduleBase + 0xC04780);

    const MH_STATUS openCreate = MH_CreateHook(
        openTarget, reinterpret_cast<void*>(&UIMenuOpenHook),
        reinterpret_cast<void**>(&g_originalUIMenuOpen));
    if (openCreate != MH_OK) {
        Log::Write(std::string("[AutoWalk] FollowController: C_UIMenu::Open hook create failed (status=") +
                   std::to_string(static_cast<int>(openCreate)) + ").");
        return;
    }

    const MH_STATUS closeCreate = MH_CreateHook(
        closeTarget, reinterpret_cast<void*>(&UIMenuCloseHook),
        reinterpret_cast<void**>(&g_originalUIMenuClose));
    if (closeCreate != MH_OK) {
        MH_RemoveHook(openTarget);
        Log::Write(std::string("[AutoWalk] FollowController: C_UIMenu::Close hook create failed (status=") +
                   std::to_string(static_cast<int>(closeCreate)) + ").");
        return;
    }

    const MH_STATUS openEnable = MH_EnableHook(openTarget);
    const MH_STATUS closeEnable = MH_EnableHook(closeTarget);
    if (openEnable != MH_OK || closeEnable != MH_OK) {
        MH_DisableHook(openTarget);
        MH_DisableHook(closeTarget);
        MH_RemoveHook(openTarget);
        MH_RemoveHook(closeTarget);
        Log::Write("[AutoWalk] FollowController: root-menu hooks could not be enabled.");
        return;
    }

    // The full-UI mode (inventory, perks, map, quest log, ...): the game's
    // source monitor broadcasts the mode transitions to
    // C_UIFullUIModeHelper::OnSourceEvent (slot [0], 0x181F52710).
    void* fullUIModeTarget = reinterpret_cast<void*>(moduleBase + 0x1F52710);
    const MH_STATUS fullUiCreate = MH_CreateHook(
        fullUIModeTarget, reinterpret_cast<void*>(&FullUIModeEventHook),
        reinterpret_cast<void**>(&g_originalFullUIModeEvent));
    if (fullUiCreate != MH_OK) {
        Log::Write(std::string("[AutoWalk] FollowController: full-UI mode hook create failed (status=") +
                   std::to_string(static_cast<int>(fullUiCreate)) + ").");
    } else {
        const MH_STATUS fullUiEnable = MH_EnableHook(fullUIModeTarget);
        if (fullUiEnable != MH_OK) {
            Log::Write("[AutoWalk] FollowController: full-UI mode hook could not be enabled.");
        }
    }

    // The wait/time dialog is a standalone Flash screen and does not enter
    // either root-menu or full-UI mode. These are the C_UISkiptime methods
    // recovered in pinned libKCD2 for KCD2 1.5.6.
    void* skipShowTarget = reinterpret_cast<void*>(moduleBase + 0xF49338);
    void* skipHideTarget = reinterpret_cast<void*>(moduleBase + 0x19C339C);
    void* skipRemoveTarget = reinterpret_cast<void*>(moduleBase + 0xB5CD84);
    bool skipHooksReady = true;

    const MH_STATUS skipShowCreate = MH_CreateHook(
        skipShowTarget, reinterpret_cast<void*>(&SkipTimeShowDialogHook),
        reinterpret_cast<void**>(&g_originalSkipTimeShowDialog));
    if (skipShowCreate != MH_OK) {
        skipHooksReady = false;
        Log::Write(std::string("[AutoWalk] FollowController: skip-time show hook create failed (status=") +
                   std::to_string(static_cast<int>(skipShowCreate)) + ").");
    }

    if (skipHooksReady) {
        const MH_STATUS skipHideCreate = MH_CreateHook(
            skipHideTarget, reinterpret_cast<void*>(&SkipTimeHideDialogHook),
            reinterpret_cast<void**>(&g_originalSkipTimeHideDialog));
        if (skipHideCreate != MH_OK) {
            skipHooksReady = false;
            MH_RemoveHook(skipShowTarget);
            Log::Write(std::string("[AutoWalk] FollowController: skip-time hide hook create failed (status=") +
                       std::to_string(static_cast<int>(skipHideCreate)) + ").");
        }
    }

    if (skipHooksReady) {
        const MH_STATUS skipRemoveCreate = MH_CreateHook(
            skipRemoveTarget, reinterpret_cast<void*>(&SkipTimeRemoveDialogHook),
            reinterpret_cast<void**>(&g_originalSkipTimeRemoveDialog));
        if (skipRemoveCreate != MH_OK) {
            skipHooksReady = false;
            MH_RemoveHook(skipShowTarget);
            MH_RemoveHook(skipHideTarget);
            Log::Write(std::string("[AutoWalk] FollowController: skip-time remove hook create failed (status=") +
                       std::to_string(static_cast<int>(skipRemoveCreate)) + ").");
        }
    }

    if (skipHooksReady) {
        const MH_STATUS showEnable = MH_EnableHook(skipShowTarget);
        const MH_STATUS hideEnable = MH_EnableHook(skipHideTarget);
        const MH_STATUS removeEnable = MH_EnableHook(skipRemoveTarget);
        if (showEnable != MH_OK || hideEnable != MH_OK || removeEnable != MH_OK) {
            skipHooksReady = false;
            MH_DisableHook(skipShowTarget);
            MH_DisableHook(skipHideTarget);
            MH_DisableHook(skipRemoveTarget);
            MH_RemoveHook(skipShowTarget);
            MH_RemoveHook(skipHideTarget);
            MH_RemoveHook(skipRemoveTarget);
            Log::Write("[AutoWalk] FollowController: skip-time hooks could not be enabled.");
        }
    }

    g_menuHooksInstalled = true;
    Log::Write(std::string("[AutoWalk] FollowController: UI lifecycle hooks installed (root menu + full-UI mode") +
               (skipHooksReady ? " + skip-time)." : "; skip-time unavailable)."));
}

class InputListener final : public Offsets::IInputEventListener {
public:
    bool OnInputEvent(const Offsets::SInputEvent& event) override
    {
        // Discovery diagnostic: which devices/keys produce non-keyboard
        // changed events (the mouse look may arrive as XI stick axes).
        if (event.deviceId != Offsets::eDI_Keyboard &&
            event.state == Offsets::eIS_Changed) {
            static std::chrono::steady_clock::time_point last{};
            const auto now = std::chrono::steady_clock::now();
            if (last == std::chrono::steady_clock::time_point{} ||
                now - last >= std::chrono::seconds(1)) {
                last = now;
                Log::Write(std::string("[AutoWalk] input: changed dev=") +
                           std::to_string(static_cast<int>(event.deviceId)) +
                           " key=" + std::to_string(event.keyId) +
                           " value=" + std::to_string(event.value));
            }
        }
        // Look axes on the mouse or XInput device: the on-foot user-look
        // source. Both shapes are recorded because KCD2 may map mouse
        // deltas onto XI stick axes for the look action (xi_rotateyaw).
        const bool lookAxis =
            (event.deviceId == Offsets::eDI_Mouse &&
             (event.keyId == Offsets::eKI_MouseX ||
              event.keyId == Offsets::eKI_MouseY)) ||
            (event.deviceId == Offsets::eDI_XI &&
             (event.keyId == Offsets::eKI_XI_ThumbRX ||
              event.keyId == Offsets::eKI_XI_ThumbRY));
        if (lookAxis) {
            if (event.value != 0.0f) {
                HorseCameraRecenter::NotifyMouseLook();
                // Proof of reception: log the first two look events with
                // their device/key so the mapping is visible in the log.
                static int proofLogged = 0;
                if (proofLogged < 2) {
                    ++proofLogged;
                    Log::Write(std::string("[AutoWalk] input: look event "
                                           "received dev=") +
                               std::to_string(static_cast<int>(event.deviceId)) +
                               " key=" + std::to_string(event.keyId) +
                               " value=" + std::to_string(event.value));
                }
            }
            return false;
        }
        return false; // never consume
    }

    bool OnInputEventUI(const void*) override { return false; }
    int GetPriority() const override { return 0; }
    bool _vf3(const void*) override { return false; }
};

InputListener s_inputListener;
bool s_listenerRegistered = false;

void LogListenerFailure()
{
    static std::chrono::steady_clock::time_point last{};
    const auto now = std::chrono::steady_clock::now();
    if (last == std::chrono::steady_clock::time_point{} ||
        now - last >= std::chrono::seconds(5)) {
        last = now;
        Log::Write("[AutoWalk] FollowController: input listener NOT "
                   "registered; camera recentering stays disabled "
                   "(fail-closed).");
    }
}

void EnsureInputListener()
{
    if (s_listenerRegistered) {
        return;
    }
    auto* env = SSystemGlobalEnvironment::GetInstance();
    if (!env || !env->pInput) {
        LogListenerFailure();
        return;
    }
    if (env->pInput->AddEventListener(&s_inputListener)) {
        s_listenerRegistered = true;
        HorseCameraRecenter::SetLookMonitoring(true);
        Log::Write("[AutoWalk] FollowController: input listener registered "
                   "(look monitoring ON).");
    } else {
        LogListenerFailure();
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

void BindPromptActions(wh::playermodule::I_ActionSets* actionSets)
{
    if (!actionSets) {
        return;
    }

    CryStringT<char> ctx("player");
    CryStringT<char> activate("foot_magnetism_activate");
    CryStringT<char> deactivate("foot_magnetism_deactivate");
    const std::function<void()> toggle(
        []() { FollowController::RequestToggle(); });

    // RegisterAction stores these callbacks in the durable registry. Context
    // rebuilds copy them into active dispatch; both same-key rows intentionally
    // share one state-aware operation regardless of which row wins the key.
    actionSets->RegisterAction(ctx, activate, toggle, 1);
    actionSets->RegisterAction(ctx, deactivate, toggle, 1);
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

    BindPromptActions(actionSets);
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
                                       : "ui_autowalk_henry_not_on_path");
    actionSets->SetActionDisableReason(ctx, activate, reason, 1);

    // [4] enabled state.
    actionSets->SetActionEnabled(ctx, activate, showActivate, 1);
    actionSets->SetActionEnabled(ctx, deactivate, showDeactivate, 1);

    // [5] visible state.
    actionSets->SetActionVisible(ctx, activate, showActivate, 1);
    actionSets->SetActionVisible(ctx, deactivate, showDeactivate, 1);

}

void RequestToggle()
{
    // Both same-key rows may dispatch in one input frame. A boolean collapses
    // duplicate callbacks into one contextual operation on the next tick.
    g_toggleRequest.Request();
}

void Enable()
{
    EnsureInputListener();
    g_toggleRequest.Block(FollowInteraction::ToggleBlocker::Context);
    g_phase.store(Phase::AwaitingRoad);
    g_smoother = {};
    g_targetYaw = 0.0f;
    g_targetValid = false;
    g_travelValid = false;
    g_followActive.store(false);
    g_cameraRecenteringEnabled.store(false);
    HorseCameraRecenter::Reset();
    ResetPositionHistory();
    RoadFollowPort::SetActionActive(true);
    Log::Write("[AutoWalk] FollowController: enabled (mode-1 engage).");
}

void Disable()
{
    g_toggleRequest.Block(FollowInteraction::ToggleBlocker::Context);
    ReleaseForward();
    RoadFollowPort::SetActionActive(false);
    ResetTravelFrame();
    g_phase.store(Phase::Disabled);
    UpdatePromptFlags(false, false, false);
    Log::Write("[AutoWalk] FollowController: disabled.");
}

void Reset()
{
    g_toggleRequest.ResetBlocked();
    ReleaseForward();
    g_rootMenuOpen.store(false);
    g_fullUIModeActive.store(false);
    g_skipTimeDialogOpen.store(false);
    RoadFollowPort::SetActionActive(false);
    ResetTravelFrame();
    g_phase.store(Phase::Disabled);
}

void Tick()
{
    EnsureMenuHooks();
    // The listener must be registered during normal hold-E play, not only
    // via the console Enable(): it is the look-input monitor the camera
    // recenter fails closed without.
    EnsureInputListener();

    auto* framework = CCryAction::GetInstance();
    auto* player = framework
        ? static_cast<wh::entitymodule::C_Player*>(framework->GetClientActor())
        : nullptr;

    if (!player || IsMounted()) {
        g_toggleRequest.Block(FollowInteraction::ToggleBlocker::Context);
        ReleaseForward();
        RoadFollowPort::SetActionActive(false);
        ResetTravelFrame();
        if (g_phase.load() != Phase::Disabled) {
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
        g_toggleRequest.Block(FollowInteraction::ToggleBlocker::Context);
        return;
    }

    // UI open (root menu, full-UI mode, or wait dialog): preserve the road-follow
    // state, but never keep our synthetic W held while the UI owns the
    // keyboard. No sampling, timers, prompt mutation, or travel-frame reset
    // occurs here.
    g_toggleRequest.Unblock(FollowInteraction::ToggleBlocker::Context);
    if (IsInputOwningUIOpen()) {
        g_cameraRecenteringEnabled.store(false);
        HorseCameraRecenter::Reset();
        ReleaseForward();
        g_lastTick = now;
        return;
    }

    // One-time native facade wiring: the sampler facade initialization.
    if (!g_nativeEnsured) {
        g_nativeEnsured = true;
        FootRoad::EnsureNativeRoadFollow();
        Log::Write(std::string("[AutoWalk] FollowController: native road-follow initialized, ready=") +
                   std::to_string(FootRoad::NativeFollowReady()));
    }

    const bool manualHeld = IsManualHeld();
    const bool chat = IsChatFollowActive();

    // The hold-E actions drive the mode-1 machine's action-controlled engage
    // bit (bit0). A completed action is consumed exactly once; invalid/manual
    // activation is rejected rather than retained until input later changes.
    const bool latchedBeforeToggle = RoadFollowPort::GetState().latched;
    const auto toggle = g_toggleRequest.Consume(
        latchedBeforeToggle, manualHeld, !chat);
    if (toggle == FollowInteraction::ToggleResolution::Disengage) {
        RoadFollowPort::SetActionActive(false);
        Disable();
        return;
    }
    if (toggle == FollowInteraction::ToggleResolution::Engage) {
        RoadFollowPort::SetActionActive(true);
        Log::Write("[AutoWalk] FollowController: engaged via hold E.");
    } else if (toggle == FollowInteraction::ToggleResolution::Cancelled) {
        Log::Write("[AutoWalk] FollowController: hold-E request canceled (manual input or invalid context).");
    }

    // The recovered mode-1 tick: the native sampler with the native
    // acquisition radius + the OnPress state machine (unwired machine ->
    // now wired).
    FootRoad::FootRoadProbe sample{};
    RoadFollowPort::Tick(dt, *cvars, manualHeld, chat, sample);
    const auto& portState = RoadFollowPort::GetState();

    const bool latched = portState.latched;
    const bool onRoad = sample.hasHit;

    if (!latched) {
        // Idle/released: clear the travel state unconditionally -- the
        // hook must stop forcing the old travel yaw. The phase conditional
        // only guards the logging.
        ReleaseForward();
        ResetTravelFrame();
        if (g_phase.load() != Phase::Disabled) {
            g_phase.store(Phase::Disabled);
            Log::Write("[AutoWalk] FollowController: native follow released.");
        }
        UpdatePromptFlags(onRoad, false, false);
        return;
    }

    const bool following = latched;
    UpdatePromptFlags(onRoad, latched, manualHeld);

    // Steering: the native command semantics. m_magnetYaw (the road
    // direction at the nearest point) becomes a relative turn command
    // against the persistent travel frame; the critically-damped smoother
    // chases that command (rate-limited, sign-flip reset) and the smoothed
    // command turns the frame each frame -- exactly the recovered
    // HorseYaw_SmoothCD -> PushRiderAction actuator shape. The movement
    // hook replaces the vanilla camera-relative velocity direction with
    // the travel frame (keeping the speed magnitude). The camera is never
    // written; the m_rootRotation body-facing seam stays pending the
    // Track A recovery of its consumer.
    if (following && !manualHeld) {
        // Sample-acceptance gate recovered from SetHoldLatchedImpl (gate 5):
        // a sample is only accepted within RoadMagnetismEnterAngle of the
        // current command. The standalone facade flips between road branches
        // at crossroads (a 100+ degree command jump); the gate rejects the
        // flips while passing slow legitimate curves -- the native
        // path-continuity behavior.
        const float rawTarget = portState.magnetYaw;
        if (!g_targetValid) {
            g_targetYaw = rawTarget;
            g_targetValid = true;
        } else if (std::abs(WrapPi(rawTarget - g_targetYaw)) * 57.2957795f <=
                   cvars->enterAngle) {
            g_targetYaw = rawTarget;
        }

        EnsureMovementHook();

        // Initialize the travel frame at Henry's current body yaw. The
        // acceptance gate was already invalidated by the release path's
        // ResetTravelFrame, so this engagement's first sample is accepted
        // unconditionally (the re-engagement stacking fix).
        if (!g_travelValid) {
            float bodyYaw = 0.0f;
            auto* framework2 = CCryAction::GetInstance();
            if (auto* entity = framework2 ? framework2->GetClientEntity() : nullptr) {
                const auto* tm = reinterpret_cast<const float*>(
                    reinterpret_cast<std::uintptr_t>(entity) + 0x58);
                // Matrix34 forward is column 1 = (m01,m11,m21) =
                // (tm[1],tm[5],tm[9]). CryEngine GetRotZ convention:
                // yaw = atan2(-fwdX, fwdY).
                bodyYaw = std::atan2(-tm[1], tm[5]);
            }
            g_travelYaw = bodyYaw;
            g_travelValid = true;
            g_smoother = {}; // the command smoother starts neutral
        }

        const float cmd = WrapPi(g_targetYaw - g_travelYaw);
        NativeMagnetism::SmoothCD(g_smoother, cmd, dt, *cvars);
        // The smoothed command chases the travel frame toward the road
        // direction: plain integration (the -1 inversion was the leftover
        // prototype knob; the log showed the travel yaw running away past
        // pi -- the circle -- with it).
        g_travelYaw += g_smoother.smoothed;

        g_followActive.store(true);
        g_cameraRecenteringEnabled.store(true);
        HoldForward();
        g_phase.store(Phase::Following);
    } else {
        ResetTravelFrame();
        ReleaseForward();
        g_phase.store(Phase::AwaitingRoad);
    }

    if (g_lastLog == steady_clock::time_point{} ||
        now - g_lastLog >= seconds(2)) {
        g_lastLog = now;
        Log::Write(std::string("[AutoWalk] follow: latched=") +
                   std::to_string(latched) +
                   " hasHit=" + std::to_string(onRoad) +
                   " live=" + std::to_string(portState.magnetismLive) +
                   " magnetYaw=" + std::to_string(portState.magnetYaw) +
                   " target=" + std::to_string(g_targetYaw) +
                   " yawFrom=" + std::to_string(sample.yawFrom) +
                   " flags=" + std::to_string(static_cast<int>(portState.flags)) +
                   " smoothed=" + std::to_string(g_smoother.smoothed) +
                    " travelYaw=" + std::to_string(g_travelYaw) +
                    " rotMax=" + std::to_string(cvars->rotationMax) +
                    " enterAngle=" + std::to_string(cvars->enterAngle) +
                    " cameraCentering=" + std::to_string(cvars->cameraCentering) +
                    " cameraPitchOffset=" +
                    std::to_string(cvars->cameraCenteringPitchOffset));
    }
}

} // namespace AutoWalk::FollowController
