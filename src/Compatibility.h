#pragma once

#include <cstdint>
#include <string>

namespace KCSE {
class IKCSEInterface;
}

namespace AutoWalk::Compatibility {

struct RuntimeInfo {
    std::uint32_t kcseVersion = 0;
    std::uint32_t gameVersion = 0;
    std::uint32_t releaseIndex = 0;
};

RuntimeInfo Capture(const KCSE::IKCSEInterface* kcse);
std::string Describe(const RuntimeInfo& info);

constexpr const char* TargetGameVersion() { return "Steam 1.5.6"; }
constexpr const char* PinnedLibKCD2Commit() { return "10d20f28faba462c4bf98a01abb48225cc51bb91"; }

} // namespace AutoWalk::Compatibility
