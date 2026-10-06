#pragma once

namespace AutoWalk::FollowController {

enum class Phase {
    Disabled,
    AwaitingRoad,
    Following,
    Suspended
};

Phase GetPhase();
void Reset();

} // namespace AutoWalk::FollowController
