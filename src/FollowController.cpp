#include "FollowController.h"

#include <atomic>

namespace AutoWalk::FollowController {
namespace {

std::atomic<Phase> g_phase{Phase::Disabled};

} // namespace

Phase GetPhase()
{
    return g_phase.load();
}

void Reset()
{
    g_phase.store(Phase::Disabled);
}

} // namespace AutoWalk::FollowController
