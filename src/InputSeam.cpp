#include "InputSeam.h"

#include <cstring>
#include <sstream>

#include "REL/Relocation.h"

#include "Log.h"
#include "Offsets/vtables/IInput.h"
#include "crysystem/CCryAction.h"
#include "crysystem/SSystemGlobalEnvironment.h"
#include "entitymodule/C_Player.h"

namespace AutoWalk::InputSeam {
namespace {

// KCD2 Steam 1.5.6 (kcd_addresslib_steam_release_1_5-15693.bin).
// See docs/REVERSE_ENGINEERING.md, Open item B.
constexpr REL::ID kIdWhGlobalGetter{50041};       // sub_1809155C8
constexpr REL::ID kIdSimActionTarget{48720};      // player vtable slot 148 body

using WhGlobalFn = void* (*)();
using SimulateOnActionTargetFn = void (*)(void* player,
                                          const void** internedName,
                                          int mode,
                                          float value);

const void* FindRegisteredAction(const char* name)
{
    if (!name || !*name) {
        return nullptr;
    }

    const auto whGlobal = REL::Relocation<WhGlobalFn>(kIdWhGlobalGetter).get();
    const auto mgr = *reinterpret_cast<std::uintptr_t*>(
        reinterpret_cast<std::uintptr_t>(whGlobal()) + 0x30);
    if (!mgr) {
        return nullptr;
    }

    // The six registered-action name slots scanned by the slot-148 gate
    // (sub_1808E56D4): +0xE8, +0x100, +0x108, +0x118, +0x128, +0x130.
    constexpr std::uint32_t kSlots[] = {0xE8, 0x100, 0x108, 0x118, 0x128, 0x130};
    for (const auto slot : kSlots) {
        const auto p = *reinterpret_cast<const void* const*>(mgr + slot);
        if (p && std::strcmp(static_cast<const char*>(p), name) == 0) {
            return p;
        }
    }
    return nullptr;
}

// KCD SInputSymbol (partial). state@+0x14 and deviceIndex@+0x25 are pinned
// by the PostInputEvent broadcast disassembly (sub_18085C264 compares/writes
// them); the remaining fields follow the stock CryEngine layout, which is
// consistent with those two verified anchors.
struct SInputSymbolRaw {
    std::uint32_t keyId;      // +0x00
    std::uint32_t _pad04;     // +0x04
    const char* name;         // +0x08
    std::uint32_t devSpecId;  // +0x10
    Offsets::EInputState state; // +0x14  VERIFIED (1=pressed,2=released,4=down,8=changed)
    std::uint32_t type;       // +0x18
    float value;              // +0x1C
    std::uint32_t user;       // +0x20
    std::uint8_t deviceId;    // +0x24
    std::uint8_t deviceIndex; // +0x25  VERIFIED
};
static_assert(offsetof(SInputSymbolRaw, state) == 0x14);
static_assert(offsetof(SInputSymbolRaw, deviceIndex) == 0x25);

SInputSymbolRaw* LookupSymbolRaw(Offsets::IInput* input,
                                 int deviceType,
                                 int deviceIndex,
                                 Offsets::EKeyId key)
{
    return const_cast<SInputSymbolRaw*>(
        static_cast<const SInputSymbolRaw*>(
            input->LookupSymbol(deviceType, deviceIndex, static_cast<int>(key))));
}

} // namespace

bool IsActionRegistered(const char* name)
{
    return FindRegisteredAction(name) != nullptr;
}

std::string DescribeRegisteredActions()
{
    std::ostringstream out;
    const auto whGlobal = REL::Relocation<WhGlobalFn>(kIdWhGlobalGetter).get();
    const auto wh = reinterpret_cast<std::uintptr_t>(whGlobal());
    const auto mgr = *reinterpret_cast<std::uintptr_t*>(wh + 0x30);

    out << "whGlobal=" << reinterpret_cast<void*>(wh)
        << " mgr=" << reinterpret_cast<void*>(mgr);
    if (!mgr) {
        out << " (null)";
        return out.str();
    }
    out << " mgrVt=" << reinterpret_cast<void*>(*reinterpret_cast<std::uintptr_t*>(mgr));

    constexpr std::uint32_t kSlots[] = {0xE8, 0x100, 0x108, 0x118, 0x128, 0x130};
    for (const auto slot : kSlots) {
        const auto p = *reinterpret_cast<const void* const*>(mgr + slot);
        out << " [" << std::hex << slot << std::dec << "]=";
        if (p) {
            out << "\"" << static_cast<const char*>(p) << "\"";
        } else {
            out << "null";
        }
    }
    return out.str();
}

bool SimulateAction(wh::entitymodule::C_Player* player,
                    const char* name,
                    int mode,
                    float value)
{
    if (!player) {
        Log::Write("[AutoWalk] SimulateAction: no player.");
        return false;
    }

    const void* interned = FindRegisteredAction(name);
    if (!interned) {
        Log::Write(std::string("[AutoWalk] SimulateAction: action '") + name +
                   "' is not currently registered; nothing simulated.");
        return false;
    }

    REL::Relocation<SimulateOnActionTargetFn>(kIdSimActionTarget)
        .get()(player, &interned, mode, value);
    return true;
}

bool PostKeyEvent(Offsets::EKeyId key, Offsets::EInputState state, float value)
{
    auto* env = SSystemGlobalEnvironment::GetInstance();
    if (!env || !env->pInput) {
        Log::Write("[AutoWalk] PostKeyEvent: input system unavailable.");
        return false;
    }

    auto* sym = LookupSymbolRaw(env->pInput, Offsets::eDI_Keyboard, 0, key);
    if (!sym) {
        Log::Write("[AutoWalk] PostKeyEvent: no symbol for key.");
        return false;
    }

    // Mirror the real device path (SInputSymbol::PressEvent + AssignTo):
    // the posted event carries pSymbol, and the broadcast keys off the
    // symbol's own state (press -> held queue, release -> removed).
    sym->state = state;
    if (state == Offsets::eIS_Pressed) {
        sym->value = 1.0f;
    } else if (state == Offsets::eIS_Released) {
        sym->value = 0.0f;
    }

    Offsets::SInputEvent evt{};
    evt.deviceId = Offsets::eDI_Keyboard;
    evt.state = state;
    evt.keyName = sym->name;
    evt.keyId = key;
    evt.modifiers = 0;
    evt.value = value;
    evt.pSymbol = reinterpret_cast<Offsets::SInputSymbol*>(sym);
    evt.deviceIndex = 0;
    env->pInput->PostInputEvent(&evt, false);
    return true;
}

bool PostMouseDelta(float dx)
{
    auto* env = SSystemGlobalEnvironment::GetInstance();
    if (!env || !env->pInput) {
        Log::Write("[AutoWalk] PostMouseDelta: input system unavailable.");
        return false;
    }

    auto* sym = LookupSymbolRaw(env->pInput, Offsets::eDI_Mouse, 0, Offsets::eKI_MouseX);
    if (!sym) {
        Log::Write("[AutoWalk] PostMouseDelta: no symbol for mouse X.");
        return false;
    }

    sym->state = Offsets::eIS_Changed;
    sym->value = dx;

    Offsets::SInputEvent evt{};
    evt.deviceId = Offsets::eDI_Mouse;
    evt.state = Offsets::eIS_Changed;
    evt.keyName = sym->name;
    evt.keyId = Offsets::eKI_MouseX;
    evt.modifiers = 0;
    evt.value = dx;
    evt.pSymbol = reinterpret_cast<Offsets::SInputSymbol*>(sym);
    evt.deviceIndex = 0;
    env->pInput->PostInputEvent(&evt, false);
    return true;
}

namespace {

std::uintptr_t VectorBegin(std::uintptr_t base)
{
    return *reinterpret_cast<std::uintptr_t*>(base);
}
std::uintptr_t VectorEnd(std::uintptr_t base)
{
    return *reinterpret_cast<std::uintptr_t*>(base + 8);
}

} // namespace

bool IsKeyHeld(Offsets::EKeyId key)
{
    auto* env = SSystemGlobalEnvironment::GetInstance();
    if (!env || !env->pInput) {
        return false;
    }

    auto* sym = LookupSymbolRaw(env->pInput, Offsets::eDI_Keyboard, 0, key);
    if (!sym) {
        return false;
    }

    const auto in = reinterpret_cast<std::uintptr_t>(env->pInput);
    const auto qBeg = VectorBegin(in + 0x28);
    const auto qEnd = VectorEnd(in + 0x28);
    for (auto p = qBeg; p < qEnd; p += 8) {
        if (*reinterpret_cast<std::uintptr_t*>(p) == reinterpret_cast<std::uintptr_t>(sym)) {
            return true;
        }
    }
    return false;
}

std::string DescribeInputPipeline()
{
    std::ostringstream out;
    auto* env = SSystemGlobalEnvironment::GetInstance();
    if (!env || !env->pInput) {
        return "inputDiag: no input system";
    }

    auto* input = env->pInput;
    const auto in = reinterpret_cast<std::uintptr_t>(input);
    out << "inputDiag pInput=" << reinterpret_cast<void*>(input)
        << " postingEnabled=" << static_cast<int>(*reinterpret_cast<std::uint8_t*>(in + 0xD8))
        << " retriggering=" << static_cast<int>(*reinterpret_cast<std::uint8_t*>(in + 0xD9))
        << " listeners=" << ((VectorEnd(in + 0x80) - VectorBegin(in + 0x80)) / 8)
        << " exclusive=" << reinterpret_cast<void*>(*reinterpret_cast<std::uintptr_t*>(in + 0xA8))
        << " consoleListeners=" << ((VectorEnd(in + 0x58) - VectorBegin(in + 0x58)) / 8);

    auto* sym = LookupSymbolRaw(input, Offsets::eDI_Keyboard, 0, Offsets::eKI_W);
    if (!sym) {
        out << " Wsym=null";
        return out.str();
    }
    out << " Wsym=" << reinterpret_cast<void*>(sym)
        << " name=\"" << (sym->name ? sym->name : "(null)") << "\""
        << " state=" << static_cast<int>(sym->state)
        << " value=" << sym->value;

    // Held queue scan before the test press.
    const auto qBeg = VectorBegin(in + 0x28);
    const auto qEnd = VectorEnd(in + 0x28);
    bool queued = false;
    for (auto p = qBeg; p < qEnd; p += 8) {
        if (*reinterpret_cast<std::uintptr_t*>(p) == reinterpret_cast<std::uintptr_t>(sym)) {
            queued = true;
            break;
        }
    }
    out << " heldQueue=" << ((qEnd - qBeg) / 8)
        << " Wqueued=" << queued;

    // One test press through the same path as PostKeyEvent.
    sym->state = Offsets::eIS_Pressed;
    sym->value = 1.0f;
    Offsets::SInputEvent evt{};
    evt.deviceId = Offsets::eDI_Keyboard;
    evt.state = Offsets::eIS_Pressed;
    evt.keyName = sym->name;
    evt.keyId = Offsets::eKI_W;
    evt.modifiers = 0;
    evt.value = 1.0f;
    evt.pSymbol = reinterpret_cast<Offsets::SInputSymbol*>(sym);
    evt.deviceIndex = 0;
    input->PostInputEvent(&evt, false);

    const auto qEnd2 = VectorEnd(in + 0x28);
    queued = false;
    for (auto p = qBeg; p < qEnd2; p += 8) {
        if (*reinterpret_cast<std::uintptr_t*>(p) == reinterpret_cast<std::uintptr_t>(sym)) {
            queued = true;
            break;
        }
    }
    out << " afterPress: state=" << static_cast<int>(sym->state)
        << " heldQueue=" << ((qEnd2 - qBeg) / 8)
        << " Wqueued=" << queued;

    // Undo the diagnostic press so the game state is left clean.
    sym->state = Offsets::eIS_Released;
    sym->value = 0.0f;
    evt.state = Offsets::eIS_Released;
    evt.value = 0.0f;
    input->PostInputEvent(&evt, false);
    out << " (test released)";
    return out.str();
}

} // namespace AutoWalk::InputSeam
