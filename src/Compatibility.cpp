#include "Compatibility.h"

#include <sstream>

#include "KCSE/KCSEAPI.h"

namespace AutoWalk::Compatibility {

RuntimeInfo Capture(const KCSE::IKCSEInterface* kcse)
{
    RuntimeInfo info{};
    if (!kcse) {
        return info;
    }

    info.kcseVersion = kcse->GetKCSEVersion();
    info.gameVersion = kcse->GetGameVersion();
    info.releaseIndex = kcse->GetReleaseIndex();
    return info;
}

std::string Describe(const RuntimeInfo& info)
{
    std::ostringstream out;
    out << "KCSE=" << info.kcseVersion
        << " gameVersionRaw=" << info.gameVersion
        << " releaseIndex=" << info.releaseIndex
        << " libKCD2=" << PinnedLibKCD2Commit()
        << " target=" << TargetGameVersion();
    return out.str();
}

} // namespace AutoWalk::Compatibility
