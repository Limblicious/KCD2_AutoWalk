#include <sstream>

#include "KCSE/KCSEAPI.h"
#include "crysystem/SSystemGlobalEnvironment.h"
#include "Offsets/vtables/IConsole.h"

#include "Compatibility.h"
#include "FollowController.h"
#include "Hooks.h"
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
        AutoWalk::Log::Write("[AutoWalk] DataLoaded.");
        break;
    case KCSE::IMessagingInterface::kMessage_LoadGame:
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
