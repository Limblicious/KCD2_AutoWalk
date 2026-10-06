#include "Hooks.h"

#include "Log.h"

namespace AutoWalk::Hooks {

bool Install()
{
    Log::Write("[AutoWalk] Hooks: instrumentation-only; no game code patched.");
    return true;
}

} // namespace AutoWalk::Hooks
