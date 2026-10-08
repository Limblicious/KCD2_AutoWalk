#include <sstream>

#include <chrono>

#include <MinHook.h>

#include "KCSE/KCSEAPI.h"
#include "CryEngine/CryCommon/IConsole.h"
#include "crysystem/CCryAction.h"
#include "crysystem/SSystemGlobalEnvironment.h"
#include "Offsets/vtables/IConsole.h"
#include "entitymodule/C_Player.h"

#include "Compatibility.h"
#include "FollowController.h"
#include "FootRoad.h"
#include "Hooks.h"
#include "InputSeam.h"
#include "Log.h"
#include "PlayerMovement.h"
#include "RoadMagnetism.h"

namespace {

int g_debug = 1;
AutoWalk::Compatibility::RuntimeInfo g_runtime{};

void PrintBoth(const std::string& line)
{
    AutoWalk::Log::Write(line);
    AutoWalk::Log::WriteLineToConsole(line);
}

void ConsoleStatus(IConsoleCmdArgs*)
{
    std::ostringstream out;
    out << "[AutoWalk] status: "
        << AutoWalk::Compatibility::Describe(g_runtime)
        << " mounted=" << AutoWalk::PlayerMovement::IsPlayerMounted()
        << " phase=" << static_cast<int>(AutoWalk::FollowController::GetPhase())
        << " log=" << AutoWalk::Log::GetLogPath();
    PrintBoth(out.str());
}

void ConsoleProbeHorse(IConsoleCmdArgs*)
{
    const auto probe = AutoWalk::RoadMagnetism::ProbeMountedHorse();
    PrintBoth("[AutoWalk] " + AutoWalk::RoadMagnetism::Describe(probe));
}

void ConsoleProbeFootRoad(IConsoleCmdArgs* args)
{
    float distance = 60.0f;
    if (args->GetArgCount() > 1) {
        distance = static_cast<float>(std::atof(args->GetArg(1)));
    }
    const auto probe = AutoWalk::FootRoad::ProbeRoadSample(distance);
    PrintBoth("[AutoWalk] " + AutoWalk::FootRoad::Describe(probe));
}

void ConsoleProbeFootRoadStandalone(IConsoleCmdArgs* args)
{
    float distance = 40.0f;
    if (args->GetArgCount() > 1) {
        distance = static_cast<float>(std::atof(args->GetArg(1)));
    }
    const auto probe = AutoWalk::FootRoad::ProbeRoadSampleStandalone(distance);
    PrintBoth("[AutoWalk] standalone: " + AutoWalk::FootRoad::Describe(probe));
}

void ConsoleSimulateAction(IConsoleCmdArgs* args)
{
    if (args->GetArgCount() < 4) {
        PrintBoth("[AutoWalk] usage: kcse_autowalk_simulate <action> <mode> <value>");
        return;
    }

    auto* framework = CCryAction::GetInstance();
    auto* player = framework
        ? static_cast<wh::entitymodule::C_Player*>(framework->GetClientActor())
        : nullptr;

    const char* name = args->GetArg(1);
    const int mode = std::atoi(args->GetArg(2));
    const float value = static_cast<float>(std::atof(args->GetArg(3)));

    const bool ok = AutoWalk::InputSeam::SimulateAction(player, name, mode, value);
    PrintBoth(std::string("[AutoWalk] simulate '") + name + "' mode=" +
              std::to_string(mode) + " value=" + std::to_string(value) +
              (ok ? " -> queued" : " -> refused"));
}

void ConsoleDumpActions(IConsoleCmdArgs*)
{
    PrintBoth("[AutoWalk] " + AutoWalk::InputSeam::DescribeRegisteredActions());
}

namespace {

std::chrono::steady_clock::time_point g_sampleDeadline{};
std::chrono::steady_clock::time_point g_nextSample{};
constexpr auto kSampleInterval = std::chrono::milliseconds(250);

void SampleActionsTick()
{
    using namespace std::chrono;
    const auto now = steady_clock::now();

    if (now >= g_nextSample) {
        AutoWalk::Log::Write("[AutoWalk] actionSample: " +
                             AutoWalk::InputSeam::DescribeRegisteredActions());
        g_nextSample = now + kSampleInterval;
    }

    if (now < g_sampleDeadline && KCSE::g_task) {
        KCSE::g_task->AddTask(&SampleActionsTick);
    } else {
        AutoWalk::Log::Write("[AutoWalk] actionSample: done.");
    }
}

} // namespace

void ConsoleSampleActions(IConsoleCmdArgs* args)
{
    using namespace std::chrono;
    const float seconds = args->GetArgCount() > 1
        ? static_cast<float>(std::atof(args->GetArg(1)))
        : 8.0f;

    g_nextSample = steady_clock::now();
    g_sampleDeadline = steady_clock::now() +
        milliseconds(static_cast<long long>(seconds * 1000.0f));

    if (!KCSE::g_task) {
        PrintBoth("[AutoWalk] actionSample: task interface unavailable.");
        return;
    }

    PrintBoth("[AutoWalk] actionSample: logging registered actions every 250ms for " +
              std::to_string(seconds) + "s; close the console and move.");
    KCSE::g_task->AddTask(&SampleActionsTick);
}

namespace {

bool g_holdWActive = false;
std::chrono::steady_clock::time_point g_holdWDeadline{};

void HoldWTick()
{
    using namespace std::chrono;
    const auto now = steady_clock::now();

    if (now < g_holdWDeadline) {
        // The game may clear key state when the console closes; keep W
        // pressed for the whole window (guarded against duplicate queue
        // entries, which would make the release ineffective).
        if (!AutoWalk::InputSeam::IsKeyHeld(Offsets::eKI_W)) {
            AutoWalk::InputSeam::PostKeyEvent(
                Offsets::eKI_W, Offsets::eIS_Pressed, 1.0f);
        }
        if (KCSE::g_task) {
            KCSE::g_task->AddTask(&HoldWTick);
        }
    } else {
        AutoWalk::InputSeam::PostKeyEvent(
            Offsets::eKI_W, Offsets::eIS_Released, 0.0f);
        g_holdWActive = false;
        AutoWalk::Log::Write("[AutoWalk] holdW: released.");
    }
}

Offsets::EKeyId KeyFromName(const char* name)
{
    if (!name) {
        return Offsets::eKI_Unknown;
    }
    if (!_stricmp(name, "w")) return Offsets::eKI_W;
    if (!_stricmp(name, "s")) return Offsets::eKI_S;
    if (!_stricmp(name, "a")) return Offsets::eKI_A;
    if (!_stricmp(name, "d")) return Offsets::eKI_D;
    if (!_stricmp(name, "shift")) return Offsets::eKI_LShift;
    if (!_stricmp(name, "space")) return Offsets::eKI_Space;
    if (!_stricmp(name, "caps")) return Offsets::eKI_CapsLock;
    if (!_stricmp(name, "left")) return Offsets::eKI_Left;
    if (!_stricmp(name, "right")) return Offsets::eKI_Right;
    if (!_stricmp(name, "up")) return Offsets::eKI_Up;
    if (!_stricmp(name, "down")) return Offsets::eKI_Down;
    return Offsets::eKI_Unknown;
}

} // namespace

void ConsoleHoldW(IConsoleCmdArgs* args)
{
    const float seconds = args->GetArgCount() > 1
        ? static_cast<float>(std::atof(args->GetArg(1)))
        : 5.0f;

    if (!KCSE::g_task) {
        PrintBoth("[AutoWalk] holdW: task interface unavailable.");
        return;
    }

    g_holdWActive = true;
    g_holdWDeadline = std::chrono::steady_clock::now() +
        std::chrono::milliseconds(static_cast<long long>(seconds * 1000.0f));

    AutoWalk::InputSeam::PostKeyEvent(Offsets::eKI_W, Offsets::eIS_Pressed, 1.0f);
    KCSE::g_task->AddTask(&HoldWTick);
    PrintBoth("[AutoWalk] holdW: holding W for " + std::to_string(seconds) +
              "s; close the console.");
}

void ConsoleKey(IConsoleCmdArgs* args)
{
    if (args->GetArgCount() < 3) {
        PrintBoth("[AutoWalk] usage: kcse_autowalk_key <w|a|s|d|shift|space|caps|left|right|up|down> <press|release>");
        return;
    }

    const auto key = KeyFromName(args->GetArg(1));
    if (key == Offsets::eKI_Unknown) {
        PrintBoth("[AutoWalk] key: unknown key name.");
        return;
    }

    const bool press = _stricmp(args->GetArg(2), "release") != 0;
    const bool ok = AutoWalk::InputSeam::PostKeyEvent(
        key, press ? Offsets::eIS_Pressed : Offsets::eIS_Released,
        press ? 1.0f : 0.0f);
    PrintBoth(std::string("[AutoWalk] key ") + args->GetArg(1) +
              (press ? " press" : " release") + (ok ? " -> posted" : " -> failed"));
}

void ConsoleMouseTurn(IConsoleCmdArgs* args)
{
    if (args->GetArgCount() < 2) {
        PrintBoth("[AutoWalk] usage: kcse_autowalk_mouseturn <dx>");
        return;
    }
    const float dx = static_cast<float>(std::atof(args->GetArg(1)));
    const bool ok = AutoWalk::InputSeam::PostMouseDelta(dx);
    PrintBoth(std::string("[AutoWalk] mouseturn dx=") + std::to_string(dx) +
              (ok ? " -> posted" : " -> failed"));
}

void ConsoleInputDiag(IConsoleCmdArgs*)
{
    PrintBoth("[AutoWalk] " + AutoWalk::InputSeam::DescribeInputPipeline());
}

namespace {

bool g_followTickArmed = false;

void FollowTick()
{
    AutoWalk::FollowController::Tick();
    // Re-enqueue forever; the tick is a no-op while disabled. Armed once per
    // game load so a single chain keeps running.
    if (KCSE::g_task) {
        KCSE::g_task->AddTask(&FollowTick);
    }
}

void ArmFollowTick()
{
    if (!g_followTickArmed && KCSE::g_task) {
        g_followTickArmed = true;
        KCSE::g_task->AddTask(&FollowTick);
    }
}

} // namespace

void ConsoleFollow(IConsoleCmdArgs* args)
{
    const bool enable = args->GetArgCount() < 2 || std::atoi(args->GetArg(1)) != 0;
    if (enable) {
        ArmFollowTick();
        AutoWalk::FollowController::Enable();
        PrintBoth("[AutoWalk] follow: enabled.");
    } else {
        AutoWalk::FollowController::Disable();
        PrintBoth("[AutoWalk] follow: disabled.");
    }
}

void RegisterConsoleSurface()
{
    auto* env = SSystemGlobalEnvironment::GetInstance();
    if (!env || !env->pConsole) {
        AutoWalk::Log::Write("[AutoWalk] Console unavailable at PreDataLoaded.");
        return;
    }

    env->pConsole->RegisterCVarInt(
        "kcse_autowalk_debug",
        &g_debug,
        g_debug,
        0,
        "KCD2 AutoWalk diagnostic verbosity.");

    env->pConsole->AddCommand(
        "kcse_autowalk_status",
        &ConsoleStatus,
        0,
        "KCD2 AutoWalk: print plugin/runtime status.");

    env->pConsole->AddCommand(
        "kcse_autowalk_probe_horse",
        &ConsoleProbeHorse,
        0,
        "KCD2 AutoWalk: read current mounted horse road-magnetism state.");

    env->pConsole->AddCommand(
        "kcse_autowalk_probe_footroad",
        &ConsoleProbeFootRoad,
        0,
        "KCD2 AutoWalk: run the native road sampler through a mounted-horse clone facade "
        "(optional search distance arg). Read-only diagnostic.");

    env->pConsole->AddCommand(
        "kcse_autowalk_probe_footroad_standalone",
        &ConsoleProbeFootRoadStandalone,
        0,
        "KCD2 AutoWalk: run the native road sampler through a synthetic zeroed facade "
        "(no horse; optional search distance arg). Diagnostic: run while standing on a road.");

    env->pConsole->AddCommand(
        "kcse_autowalk_simulate",
        &ConsoleSimulateAction,
        0,
        "KCD2 AutoWalk: simulate an action via the SimulateOnAction seam "
        "(kcse_autowalk_simulate <action> <mode> <value>). Diagnostic; single-shot.");

    env->pConsole->AddCommand(
        "kcse_autowalk_dump_actions",
        &ConsoleDumpActions,
        0,
        "KCD2 AutoWalk: dump the six registered-action name slots and manager identity.");

    env->pConsole->AddCommand(
        "kcse_autowalk_sample_actions",
        &ConsoleSampleActions,
        0,
        "KCD2 AutoWalk: log the registered-action slots every 250ms for N seconds "
        "(default 8) while you move. Close the console and play normally.");

    env->pConsole->AddCommand(
        "kcse_autowalk_hold_w",
        &ConsoleHoldW,
        0,
        "KCD2 AutoWalk: hold W through the input pipeline for N seconds (default 5). "
        "Close the console after running.");

    env->pConsole->AddCommand(
        "kcse_autowalk_key",
        &ConsoleKey,
        0,
        "KCD2 AutoWalk: post a single key event through the input pipeline "
        "(kcse_autowalk_key <w|a|s|d|shift|space|caps|left|right|up|down> <press|release>).");

    env->pConsole->AddCommand(
        "kcse_autowalk_mouseturn",
        &ConsoleMouseTurn,
        0,
        "KCD2 AutoWalk: post a mouse X delta event (kcse_autowalk_mouseturn <dx>).");

    env->pConsole->AddCommand(
        "kcse_autowalk_input_diag",
        &ConsoleInputDiag,
        0,
        "KCD2 AutoWalk: input pipeline diagnostic (symbol, posting flag, listeners, "
        "held queue, one test press).");

    int* invert = &AutoWalk::FollowController::g_steerInvert;
    env->pConsole->RegisterCVarInt(
        "kcse_autowalk_steer_invert",
        invert,
        1,
        0,
        "KCD2 AutoWalk: 1 to flip the steering direction.");

    env->pConsole->AddCommand(
        "kcse_autowalk_follow",
        &ConsoleFollow,
        0,
        "KCD2 AutoWalk: enable/disable on-foot road following "
        "(kcse_autowalk_follow <1|0>).");
}

void OnMessage(KCSE::Message* message)
{
    if (!message) {
        return;
    }

    switch (message->type) {
    case KCSE::IMessagingInterface::kMessage_PreDataLoaded:
        RegisterConsoleSurface();
        AutoWalk::Log::Write("[AutoWalk] PreDataLoaded.");
        break;
    case KCSE::IMessagingInterface::kMessage_DataLoaded:
        AutoWalk::FollowController::RegisterPromptActions();
        AutoWalk::Log::Write("[AutoWalk] DataLoaded.");
        break;
    case KCSE::IMessagingInterface::kMessage_LoadGame:
        if (g_holdWActive) {
            AutoWalk::InputSeam::PostKeyEvent(
                Offsets::eKI_W, Offsets::eIS_Released, 0.0f);
            g_holdWActive = false;
            AutoWalk::Log::Write("[AutoWalk] LoadGame: released held W.");
        }
        ArmFollowTick();
        AutoWalk::FollowController::RegisterPromptActions();
        AutoWalk::FollowController::Reset();
        AutoWalk::Log::Write("[AutoWalk] LoadGame: controller reset.");
        break;
    case KCSE::IMessagingInterface::kMessage_NewGame:
        AutoWalk::FollowController::Reset();
        AutoWalk::Log::Write("[AutoWalk] NewGame: controller reset.");
        break;
    default:
        break;
    }
}

} // namespace

KCSE_PLUGIN_INFO("KCD2_AutoWalk", "Limblicious", 1);

KCSE_PLUGIN_LOAD(kcse)
{
    AutoWalk::Log::Initialize();

    if (MH_Initialize() != MH_OK) {
        AutoWalk::Log::Write("[AutoWalk] MinHook init failed.");
        return false;
    }

    g_runtime = AutoWalk::Compatibility::Capture(kcse);
    AutoWalk::Log::Write("[AutoWalk] Plugin load.");
    AutoWalk::Log::Write("[AutoWalk] " + AutoWalk::Compatibility::Describe(g_runtime));

    if (!AutoWalk::Hooks::Install()) {
        AutoWalk::Log::Write("[AutoWalk] Hook initialization failed; refusing plugin load.");
        return false;
    }

    auto* messaging = kcse ? kcse->GetMessagingInterface() : nullptr;
    if (!messaging) {
        AutoWalk::Log::Write("[AutoWalk] KCSE messaging interface unavailable.");
        return false;
    }

    if (!messaging->RegisterListener(&OnMessage)) {
        AutoWalk::Log::Write("[AutoWalk] Failed to register KCSE message listener.");
        return false;
    }

    AutoWalk::Log::Write("[AutoWalk] Ready: instrumentation milestone.");
    return true;
}
