#include "Log.h"

#include <Windows.h>

#include <filesystem>
#include <fstream>
#include <mutex>

#include "crysystem/SSystemGlobalEnvironment.h"
#include "Offsets/vtables/IConsole.h"

namespace AutoWalk::Log {
namespace {

std::mutex g_mutex;
std::filesystem::path g_logPath;
bool g_initialized = false;

std::filesystem::path ModulePath()
{
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&ModulePath),
            &module)) {
        return {};
    }

    std::wstring buffer(32768, L'\0');
    const DWORD length = GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) {
        return {};
    }

    buffer.resize(length);
    return std::filesystem::path(buffer);
}

std::filesystem::path ResolveLogPath()
{
    const auto dll = ModulePath();
    if (dll.empty()) {
        return {};
    }

    // <mod>/KCSE/Plugins/KCD2_AutoWalk.dll -> <mod>/KCD2_AutoWalk.log
    const auto plugins = dll.parent_path();
    const auto kcse = plugins.parent_path();
    const auto mod = kcse.parent_path();
    if (mod.empty()) {
        return {};
    }

    return mod / "KCD2_AutoWalk.log";
}

} // namespace

bool Initialize()
{
    std::scoped_lock lock(g_mutex);
    if (g_initialized) {
        return true;
    }

    g_logPath = ResolveLogPath();
    if (g_logPath.empty()) {
        return false;
    }

    std::ofstream stream(g_logPath, std::ios::out | std::ios::app);
    if (!stream) {
        return false;
    }

    stream << "=== KCD2_AutoWalk session ===\n";
    stream.flush();
    g_initialized = true;
    return true;
}

void WriteLineToConsole(std::string_view message)
{
    auto* env = SSystemGlobalEnvironment::GetInstance();
    if (!env || !env->pConsole) {
        return;
    }

    std::string owned(message);
    env->pConsole->PrintLine(owned.c_str());
}

void Write(std::string_view message)
{
    {
        std::scoped_lock lock(g_mutex);
        if (!g_initialized) {
            g_logPath = ResolveLogPath();
            g_initialized = !g_logPath.empty();
        }

        if (g_initialized) {
            std::ofstream stream(g_logPath, std::ios::out | std::ios::app);
            if (stream) {
                stream << message << '\n';
                stream.flush();
            }
        }
    }

    std::string debug(message);
    debug.push_back('\n');
    OutputDebugStringA(debug.c_str());
}

std::string GetLogPath()
{
    std::scoped_lock lock(g_mutex);
    return g_logPath.string();
}

} // namespace AutoWalk::Log
