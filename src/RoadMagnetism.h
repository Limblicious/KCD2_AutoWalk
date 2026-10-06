#pragma once

#include <string>

namespace AutoWalk::RoadMagnetism {

struct MountedProbe {
    bool playerFound = false;
    bool mounted = false;
    bool horseDataFound = false;
    bool magnetismLive = false;
    bool controllerPresent = false;
    int mode = 0;
    float rawYaw = 0.0f;
    float smoothedYaw = 0.0f;
    float hitX = 0.0f;
    float hitY = 0.0f;
    float hitZ = 0.0f;
};

MountedProbe ProbeMountedHorse();
std::string Describe(const MountedProbe& probe);

// Direct on-foot road sampling is intentionally disabled until the native
// sample function's complete call contract is verified.

} // namespace AutoWalk::RoadMagnetism
