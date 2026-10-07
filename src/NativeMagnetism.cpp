#include "NativeMagnetism.h"

#include <cmath>

#include "REL/Relocation.h"

#include "Log.h"

namespace AutoWalk::NativeMagnetism {
namespace {

// Frame helper getter (the object every recovered CVar read uses).
constexpr REL::ID kIdFrameHelper{38017}; // sub_1806CDCF4
using FrameHelperFn = void* (*)();

FrameCVars g_cvars{};
bool g_cvarsValid = false;

// SmoothCD constants recovered from WHGame.dll .rdata (1.5.6):
// 0x18409A2D8 = 2.0, 0x18409F44 = 1.0, 0x18409EE60 = 0.4805,
// 0x18409EE64 = 0.2352, 0x18409A4A8 = 1.68e-7 (eps).
constexpr float kCdConstA = 0.4805f;
constexpr float kCdConstB = 0.2352f;
constexpr float kCdEps = 1.68e-7f;

} // namespace

bool RefreshFrameCVars(const FrameCVars*& out)
{
    const auto fh = REL::Relocation<FrameHelperFn>(kIdFrameHelper).get();
    if (!fh) {
        out = g_cvarsValid ? &g_cvars : nullptr;
        return g_cvarsValid;
    }
    const auto base = reinterpret_cast<std::uintptr_t>(fh());
    if (!base) {
        out = g_cvarsValid ? &g_cvars : nullptr;
        return g_cvarsValid;
    }

    auto read = [&](std::uint32_t off) {
        return *reinterpret_cast<const float*>(base + off);
    };

    g_cvars.enterAngle = read(0x84);
    g_cvars.remainAngle = read(0x88);
    g_cvars.snapTime = read(0x98);
    g_cvars.rotationMax = read(0xE8);
    g_cvars.smoothOutSpeed = read(0xEC);
    g_cvars.smoothInSpeed = read(0xF0);
    g_cvars.clampDelta = read(0x16C);
    g_cvars.deactivateTime = read(0x204);
    g_cvars.reactivateTime = read(0x208);
    g_cvarsValid = true;
    out = &g_cvars;
    return true;
}

void SmoothCD(YawSmoother& s, float target, float dt, const FrameCVars& cvars)
{
    float f7 = dt;
    if (cvars.clampDelta < f7) {
        f7 = cvars.clampDelta;
    }

    // Sign-flip reset (target vs current smoothed), exactly like the native.
    if (target != 0.0f &&
        std::signbit(target) != std::signbit(s.smoothed)) {
        s.smoothed = 0.0f;
    }

    const float prev = s.smoothed;
    float f6 = prev;

    if (kCdEps < std::abs(target - prev)) {
        const float omega = f7 * cvars.rotationMax;
        // Rate-limit the STEP toward the target (the vanilla clamps the
        // per-frame advance, NOT the absolute position): f6 = the advanced
        // position the critically-damped state chases.
        float step = target - prev;
        if (step <= -omega) step = -omega;
        if (omega <= step) step = omega;
        f6 = prev + step;

        const float speed = (target == 0.0f) ? cvars.smoothOutSpeed
                                             : cvars.smoothInSpeed;
        float vel = s.vel;
        if (speed <= 0.0f) {
            vel = f7 > 0.0f ? (f6 - prev) / f7 : 0.0f;
        } else {
            const float f8 = 2.0f / speed;
            float f9 = f8 * f7;
            f9 = 1.0f / (f9 * kCdConstA * f9 + f9 + 1.0f +
                         f9 * kCdConstB * f9 * f9);
            const float f7v = ((prev - f6) * f8 + s.vel) * f7;
            f6 = (f7v + (prev - f6)) * f9 + f6;
            vel = (s.vel - f7v * f8) * f9;
        }
        s.vel = vel;
        s.smoothed = f6;
    }
}

bool TickStateMachine(OnPressState& st, float dt, const FrameCVars& cvars,
                      bool manualInputActive, bool chatFollowActive,
                      bool sampleFailed, bool engageRequested)
{
    const bool active = (st.flags & 0x01) != 0;
    const bool armed = (st.flags & 0x10) != 0;

    if (chatFollowActive) {
        st.flags &= ~0x01; // clear active
        st.flags &= ~0x02; // clear latched
        st.flags &= ~0x10;
        st.deactivateTime = 0.0f;
        st.reactivateTime = 0.0f;
        return false;
    }

    if (!active) {
        if (engageRequested) {
            st.flags |= 0x01; // active
            st.flags |= 0x02; // latched
            st.flags &= ~0x10;
            st.deactivateTime = 0.0f;
            st.reactivateTime = 0.0f;
        }
        return (st.flags & 0x01) != 0;
    }

    if (sampleFailed) {
        st.flags &= ~0x01; // clear active
        st.flags &= ~0x02;
        st.flags |= 0x08;  // flick
        st.flags &= ~0x10;
        st.deactivateTime = 0.0f;
        st.reactivateTime = 0.0f;
        return false;
    }

    // Phase 2: manual input beyond tolerance arms deactivation timers.
    if (manualInputActive && !armed) {
        st.flags |= 0x10;
        st.deactivateTime = cvars.deactivateTime;
        st.reactivateTime = cvars.reactivateTime;
    } else if (!manualInputActive && armed) {
        // Phase 0: input returned to neutral clears the armed state.
        st.flags &= ~0x10;
        st.deactivateTime = 0.0f;
        st.reactivateTime = 0.0f;
    }

    // Phase 3: countdown; deactivate after the reactivate grace window.
    if (armed) {
        if (st.deactivateTime > 0.0f) {
            st.deactivateTime -= dt;
            if (st.deactivateTime < 0.0f) st.deactivateTime = 0.0f;
        } else if (st.reactivateTime > 0.0f) {
            st.reactivateTime -= dt;
            if (st.reactivateTime < 0.0f) st.reactivateTime = 0.0f;
        } else {
            st.flags &= ~0x01;
            st.flags &= ~0x02;
            st.flags &= ~0x10;
            return false;
        }
    }

    if (st.hintTime > 0.0f) {
        st.hintTime -= dt;
        if (st.hintTime < 0.0f) st.hintTime = 0.0f;
    }

    return true;
}

} // namespace AutoWalk::NativeMagnetism
