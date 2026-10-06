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

## Open item B — on-foot movement injection

Locate the point where physical on-foot movement actions become Henry's normalized movement vector after control resolution but before locomotion consumes it.

Acceptance:

- camera remains free;
- movement magnitude is normalized;
- Caps Lock and Shift remain vanilla;
- stamina/collision/animation remain vanilla;
- disabling cannot leave stuck movement.

## Evidence standard

For each native function/hook record game build, module, REL ID/signature, prototype, fields read/written, validation, failure behavior, and local runtime evidence.
