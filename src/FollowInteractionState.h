#pragma once

#include <atomic>

namespace AutoWalk::FollowInteraction {

enum class ToggleResolution {
    None,
    Engage,
    Disengage,
    Cancelled,
};

enum class ToggleBlocker : unsigned {
    RootMenu = 0x01,
    FullUI = 0x02,
    SkipTime = 0x04,
    Context = 0x08,
};

// A contextual hold-E completion is valid for one controller tick. Duplicate
// callbacks collapse, but an invalid activation is rejected rather than kept
// armed until unrelated input later becomes neutral.
class ToggleRequest {
public:
    void Request()
    {
        unsigned state = m_state.load();
        while ((state & kBlockMask) == 0) {
            if (m_state.compare_exchange_weak(state, state | kPending)) {
                return;
            }
        }
    }
    void Cancel() { m_state.fetch_and(~kPending); }
    void Block(ToggleBlocker blocker)
    {
        m_state.fetch_or(static_cast<unsigned>(blocker));
        m_state.fetch_and(~kPending);
    }
    void Unblock(ToggleBlocker blocker)
    {
        m_state.fetch_and(~static_cast<unsigned>(blocker));
    }
    void ResetBlocked() { m_state.store(kContext); }
    bool IsPending() const { return (m_state.load() & kPending) != 0; }
    bool IsBlocked() const { return (m_state.load() & kBlockMask) != 0; }

    ToggleResolution Consume(bool latched, bool manualHeld, bool actionValid)
    {
        unsigned expected = kPending;
        if (!m_state.compare_exchange_strong(expected, 0)) {
            return ToggleResolution::None;
        }
        if (!actionValid || (!latched && manualHeld)) {
            return ToggleResolution::Cancelled;
        }
        return latched ? ToggleResolution::Disengage
                       : ToggleResolution::Engage;
    }

private:
    static constexpr unsigned kContext =
        static_cast<unsigned>(ToggleBlocker::Context);
    static constexpr unsigned kBlockMask = 0x0F;
    static constexpr unsigned kPending = 0x10;
    std::atomic<unsigned> m_state{kContext};
};

struct ManualWResolution {
    bool manualHeld;
    bool releaseSynthetic;
};

// Synthetic and physical W share one engine symbol. Transfer that symbol to
// the player when the physical key is down; otherwise release plugin ownership.
inline ManualWResolution ResolveManualW(bool syntheticHeld,
                                        bool engineHeld,
                                        bool physicalHeld)
{
    return {physicalHeld,
            syntheticHeld && engineHeld && !physicalHeld};
}

} // namespace AutoWalk::FollowInteraction
