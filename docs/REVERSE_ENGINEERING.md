# Reverse Engineering Record

This file separates public RE evidence from assumptions.

## Baseline

- libKCD2: https://github.com/JerryYOJ/libKCD2
- pinned commit: `10d20f28faba462c4bf98a01abb48225cc51bb91`
- upstream stated native target: KCD2 Steam 1.5.6

## S_HorseData

Header:
https://github.com/JerryYOJ/libKCD2/blob/10d20f28faba462c4bf98a01abb48225cc51bb91/include/entitymodule/S_HorseData.h

| Field | Offset | Meaning |
|---|---:|---|
| `m_pMove` | `+0x100` | horse move adapter |
| `m_yawSmoothed` | `+0x128` | smoothed steering state |
| `m_magnetYaw` | `+0x130` | raw magnetism yaw command |
| `m_magnetismLive` | `+0x138` | magnetism active |
| `m_magnetHit` | `+0x13C` | magnetism road hit |
| `m_roadFollow` | `+0x158` | embedded road-follow state |

## S_HorseRoadFollow

Header:
https://github.com/JerryYOJ/libKCD2/blob/10d20f28faba462c4bf98a01abb48225cc51bb91/include/entitymodule/S_HorseRoadFollow.h

- `m_pMagnetism +0x68`
- `m_mode +0x70`: 0 off / 1 on-press / 2 auto
- pinned comments identify tick `sub_180A4E5AC`

## I_MagnetismController

Header:
https://github.com/JerryYOJ/libKCD2/blob/10d20f28faba462c4bf98a01abb48225cc51bb91/include/entitymodule/I_MagnetismController.h

```cpp
SetHoldLatched(bool)
Tick(S_HorseMagnetismSample* sample, int phase, float dt)
GetRoadDistance(bool deactivate)
```

Mapped implementors:

- `S_OnPressController`
- `S_AutoController`

## S_HorseMagnetismSample

Header:
https://github.com/JerryYOJ/libKCD2/blob/10d20f28faba462c4bf98a01abb48225cc51bb91/include/entitymodule/S_HorseMagnetismSample.h

| Field | Offset |
|---|---:|
| `m_hit` | `+0x00` |
| `m_along` | `+0x0C` |
| `m_pFrom` | `+0x18` |
| `m_pTo` | `+0x20` |
| `m_yawFrom` | `+0x28` |
| `m_yawTo` | `+0x2C` |
| `m_hasHit` | `+0x30` |
| `m_failed` | `+0x31` |

Pinned comments identify `sub_180A0A124` as the sample-filling function for its 1.5.6 baseline.

## C_RiderPlayerInput

Header:
https://github.com/JerryYOJ/libKCD2/blob/10d20f28faba462c4bf98a01abb48225cc51bb91/include/entitymodule/C_RiderPlayerInput.h

Relevant mapped members include `m_move`, `m_turn`, `m_stickMag`, and `m_updateArmed`.

JerryYOJ's BetterHorseHandling plugin uses these fields at runtime and checks `S_HorseData::m_magnetismLive`, providing useful precedent.

## Game-data evidence

Public KCD2 data/docs expose:

- `no_horse_magnetism_activate`;
- `AutomaticRoadMagnetism`;
- `HorseMagnetismTurnSlowdown`.

That supports treating road magnetism as a dedicated native subsystem.

## Current instrumentation

`RoadMagnetism::ProbeMountedHorse()` reads only known mapped fields:

- live state;
- raw and smoothed yaw;
- road hit;
- road-follow mode;
- controller presence.

It does not call or patch road-follow code.

## Open item A — direct road sampler

Before invoking `sub_180A0A124`:

1. recover exact prototype/calling convention;
2. identify every argument;
3. identify road-point/path state ownership;
4. establish whether it can sample from Henry without a live horse;
5. find an Address Library `REL::ID`, if present;
6. otherwise define a validated signature/fingerprint;
7. document fail-closed behavior.

Never call the raw RVA.

### Resolved call chain (KCD2 Steam 1.5.6, kcd_addresslib_steam_release_1_5-15693.bin)

`S_HorseRoadFollow::Tick` (sub_180A4E5AC, **REL::ID 56405**) calls the road
sampling wrapper (sub_181ED8860, **REL::ID 194146**) at call site 0x180A4E62D:

```text
wrapper(rcx = S_HorseRoadFollow* self, xmm2 = float searchDistance,
        r9 = S_HorseMagnetismSample* out) -> bool
```

The 2nd register arg (rdx) is NOT consumed by the wrapper (standard x64 ABI,
verified by the tick's register setup and the wrapper's body). The wrapper:

1. fetches the global road system: `whGlobal() (+0x160) (+0x78) vf[8] -> roadSystem`,
   `roadSystem vf[0x70] -> graph` (`whGlobal` = sub_1809155C8, **REL::ID 50041**);
2. calls the state builder (sub_181ED7ED0, **REL::ID 194136**) which:
   - reads `self->m_pHorseData (+0xF8) -> C_Horse*`, then `C_Horse+0x38 -> IEntity*`;
   - calls `IEntity` vf[34] GetWorldBounds (output unused), vf[46] GetWorldPos
     (query position), vf[48] GetWorldRotation-ish (orientation quat);
   - reads road-controller half-width floats at roadController +0x13C/+0x140;
   - runs a global road box query (sub_181ECF510) producing the from/to road
     points and emits a 32-byte state {pFrom, pTo, Vec3 pos};
3. calls the sampler (sub_180A0A124, **REL::ID 55123**):

```text
sampler(rcx = self, rdx = graph, r8 = &state{pFrom,pTo,pos}, r9 = unused,
        [rsp+0x20] = S_HorseMagnetismSample* out) -> bool
```

The sampler additionally reads `S_HorseData+0x110` (m_pseudoSpeed) and
`C_Horse+0x668` / embedded `C_Horse+0x7A0` (+0x188 int array, road-state) and
writes `out->m_hit / m_along / m_pFrom / m_pTo / m_yawFrom / m_yawTo / m_hasHit`.
It performs NO writes to `self`/horse state (read-only with respect to the
facade; only the caller-owned `out` buffer is written).

### On-foot applicability

The only per-entity inputs are: the IEntity position/orientation getters
(any KCD2 entity satisfies the mapped IEntity vtable slots), `m_pseudoSpeed`,
and the C_Horse road-state fields. A **clone facade** (deep-copy of a live
mounted horse's `S_HorseData` (0x4C8) + `S_HorseRoadFollow` (0x80) +
`C_Horse` (0xA60), with only `C_Horse+0x38` redirected to Henry's `IEntity*`
and the pointer graph retargeted at the copies) is fully validated: every
field originates from a live object. This is implemented as the
`kcse_autowalk_probe_footroad` diagnostic (mounted precondition, read-only).

A **zeroed standalone facade** (true on-foot, no horse present) still has one
unverified null path: `C_Horse+0x668` is passed to the global road-lookup
`vf[4]` (0x1807FE804); that function has a static fallback when the inner
lookup fails, but null-tolerance of the inner sub_1807FE964 is unproven.
Zeroed-facade probing is deliberately NOT implemented until the clone probe
validates the chain end-to-end.

## Open item B — on-foot forward input and steering

Find the least invasive on-foot seams that let AutoWalk:

1. sustain ordinary forward movement, equivalent in intent to holding `W`; and
2. apply the road-follow heading/turn correction to Henry.

Do not solve this by creating a camera-relative movement vector or a horse-style independent camera mode.

Acceptance:

- straight-line synthetic forward movement behaves like ordinary held forward input;
- native road steering can turn Henry toward the road heading;
- KCD2's normal on-foot camera/facing coupling remains untouched;
- the mod does not directly rotate, lock, or decouple the camera;
- Caps Lock and Shift remain vanilla;
- stamina/collision/animation remain vanilla;
- disabling cannot leave stuck movement or steering state.

### Resolved input seam (KCD2 Steam 1.5.6)

Lua `actor:SimulateOnAction(action, mode, value)` is
`C_ScriptBindActor::SimulateOnAction` (sub_182AC3010, vtable slot s23,
**REL::ID 352389**). Its body:

1. resolves the actor entity from the action key (sub_18082B834, REL 45048:
   action-key vf[2] -> entity id -> IGame vf[0xC8] -> entity system -> GetEntity);
2. interns the action name into a ref-counted string object
   (sub_1808E5AEC, **REL::ID 48734**; string bytes at ptr, refcount at ptr-0xC);
3. calls **entity vtable slot 148** (sub_1808E5618, **REL::ID 48720**):

```text
playerVf148(C_Player* self, const void** internedName, int mode, float value)
```

The slot-148 body gates on pointer equality of `*internedName` against six
registered-action slots of the manager at `whGlobal()+0x30`
(+0xE8/+0x100/+0x108/+0x118/+0x128/+0x130; gate sub_1808E56D4,
**REL::ID 48726**); on match it appends {name, mode, value} to the simulated-
action queue vector at `C_Player+0xAC0` (consumed by the player input update;
`activationMode: eAAM_OnPress = 1` per Offsets/vtables/IActionMapManager.h).

The same seam applies to any registered action name (e.g. `moveforward`,
`xi_rotateyaw`): resolve the interned pointer from the six manager slots,
then call REL::ID 48720 on the client player. Implemented as the
`kcse_autowalk_simulate <name> <mode> <value>` diagnostic (console-gated,
single-shot; this is the movement-mutation seam used only for verification).

Key REL IDs used (Steam 1.5.6):

| Symbol | REL::ID |
|---|---|
| whGlobal getter (sub_1809155C8) | 50041 |
| SimulateOnAction slot-148 target | 48720 |
| slot-148 gate | 48726 |
| action-name interner | 48734 |
| SimulateOnAction actor resolver | 45048 |
| SimulateOnAction (script bind body) | 352389 |
| road sample wrapper (sub_181ED8860) | 194146 |
| road sample state builder (sub_181ED7ED0) | 194136 |
| road sampler (sub_180A0A124) | 55123 |
| S_HorseRoadFollow::Tick (sub_180A4E5AC) | 56405 |
| C_RiderPlayerInput::Update (precedent, BetterHorseHandling) | 56411 |

## Evidence standard

For each native function/hook record game build, module, REL ID/signature, prototype, fields read/written, validation, failure behavior, and local runtime evidence.


## Decompiler-first policy

The existing public mappings are seeds, not a substitute for reconstructing the complete native implementation.

The next reverse-engineering phase is defined in `docs/DECOMPILATION_PLAN.md`.

Do not infer controller behavior from low-frequency runtime probes when the corresponding machine code can be decompiled. Runtime probes are reserved for validating recovered control flow and state transitions at native tick cadence.

The final AutoWalk controller must be explainable as either:

1. execution of the original native controller against a synthetic Henry facade; or
2. a direct translation of fully recovered native branches/state where direct execution is unsafe.

A custom tangent/cross-track/PID controller is not considered equivalent.
