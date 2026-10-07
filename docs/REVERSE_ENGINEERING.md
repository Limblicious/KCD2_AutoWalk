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

## Native reconstruction (Ghidra 12.1.4, staged .re copy)

Recovered from the exact staged binary via Ghidra + GhidraMCP, seeded with
libKCD2/upstream RE findings. Struct types live in the Ghidra project under
`/AutoWalk/`.

### Layout correction vs seeds

`S_AutoController` (0x48): the seed manifest placed `m_path` at +0x20. The
ctor and phase-4/phase-5 handlers prove the path vector is at **+0x08**
({begin,end,capacity} at +0x08/+0x10/+0x18); +0x20 is a second reserved
vector region initialized with the same reserve helper.

### Function: S_AutoController::Tick
- VA: 0x1829F2558; REL 554715 slot 2 (vtable 0x183EAAE18)
- Recovered signature: `void Tick(AW_S_AutoController* this, S_HorseMagnetismSample* sample, int phase, float dt)`
- Branches: phase 1 -> countdown `m_timer` (clamped at 0) then `TickPhase1`; phase 5 -> clear path vector (end=begin); all other phases no-op.
- Output: none directly; side effects via phase-1 body.

### Function: S_AutoController::TickPhase1 (0x1829F16FC)
- Inputs: `this` (controller), implicit `gEnv->vf[3]()` current time.
- Persistent reads: m_path {begin,end,cap}; m_flag44; m_pHorseData(+0xF8 C_Horse; +0x7A0 road state; +0x124 backwards handled elsewhere).
- Persistent writes: appends `{m_flag44, now}` records (8-byte: byte flag@0, float time@4) when `dtSinceLastRecord > 0.2f` (const 0x3E4CCCCD); prunes records older than `frameHelper+0x98` (RoadMagnetismSnapTime) via lower_bound; clears the path when the gate fails.
- Gate: `Horse_ModelQuery(horse) == false && RoadState_GetIndex(horse+0x7A0, tag) != 4`.
- Fallback dt when path empty: FLT_MAX (0x7F7FFFFF).
- Callees: HorseData_GetTag, RoadState_GetIndex, Horse_ModelQuery (0x181E7D660, jumps to actor-model vf[0x298](1)), PathHistory_PruneOlderThan, vector push helpers, vector erase helper.
- Semantics: maintains a timestamp/flag history of road-latch moments inside the SnapTime window; used by the flick scorer and enter/remain checks.

### Function: S_AutoController::SetHoldLatched (slot 1, 0x1829F1C40 -> SetHoldLatchedImpl 0x1829F1C70)
- Recovered signature: `bool SetHoldLatched(AW_S_AutoController* this, bool latched, bool hit, S_HorseMagnetismSample* sample)` (4 args; earlier header mapping was wrong).
- Writes: m_flag44 = hit.
- Gates (any failure returns false):
  1. horse move adapter valid (`HorseData_GetTag` chain, 0x1806CCCD4/0x1804A8CE0);
  2. **chat-follow inactive** (`[whGlobal+8]->vf[0x200]() -> [..+0xCE8] -> vf[8]` — C_Player::m_pChatFollowManager follow check);
  3. if not latched: builds a vector {pos (IEntity vf[0x170] GetWorldPos), + m_along when hit} and runs RoadSnap_TestVector (0x180A4E208) — non-zero result fails;
  4. rider input: `m_pMove(+0x100)->vf[0x18]()` yields a yaw; `|wrap(yaw)|*deg < frame+0x88` (RoadMagnetismRemainAngle);
  5. if hit && !latched: `min(|yawFrom-m_magnetYaw|,|yawTo-m_magnetYaw|)*deg` must be `<= frame+0x84` (RoadMagnetismEnterAngle);
  6. walks path history backward within the SnapTime window; for each flagged record: PathHistory_FlickScore returns 4 -> fail; if not backwards (+0x124 == 0): Road_SnapChooser(0x060B0A00, this) non-zero -> **return true**.
- Semantics: exact enter/remain hysteresis; the native "hold E" latch acceptance.

### Function: S_HorseRoadFollow::Tick (0x180A4E5AC, REL 56405)
- Recovered signature: `bool Tick(AW_S_HorseRoadFollow* this, float dt)` (returns m_latched).
- Flow (matches the earlier hand-recovered call sequence exactly, plus the two extra publish helpers):
  1. HorseRoadFollow_RebuildControllerMode(this) — mode 0/1/2 controller selection from options;
  2. controller absent -> return latched;
  3. zero a stack S_HorseMagnetismSample (0x38);
  4. `dist = ctrl->GetRoadDistance(m_latched)`;
  5. `hit = HorseRoadSample_Wrapper(this, dist, &sample)`;
  6. `ctrl->Tick(&sample, 0, dt)`;
  7. `ok = ctrl->SetHoldLatched(m_latched, hit, &sample)`;
  8. if !hit || !ok: stick reset (m_stickDelta=0; 0x180A03994(&stick,2,&static)); m_magnetismLive=0; if latched {pathA clear; Tick(5)}; latched=0; Tick(3);
  9. else: if !latched Tick(4); latched=1; Tick(2); then HorseRoadFollow_UpdateTurnParams, HorseRoadFollow_PushPathB, HorseRoadFollow_PublishMagnetism;
  10. Tick(1); return m_latched.
- Persistent writes: m_latched; m_stick; m_pathA; via helpers: m_magnetismLive/m_magnetHit/m_magnetYaw (+0x130 is sample.m_yawFrom — a command, not world heading), m_fastStop(+0x10B), m_pathB, horse turn-rate params.

### Function: HorseRoadFollow_PublishMagnetism (0x180A4DE5C)
- Writes: +0x138 m_magnetismLive = sample.m_hasHit; +0x13C m_magnetHit = sample.m_hit; +0x130 m_magnetYaw = sample.m_yawFrom.
- Turn-class selection: picks min id in {2,1,0} whose frame threshold (+0xB0/+0xB4/+0xB8 = Dash/Sprint/Run MaxDegree cvars) exceeds `|wrap(yawFrom-yawTo)|*deg`; then conditionally writes turn-rate params through `FUN_180a5019c(S_HorseData+0x148, ...)` (horse movement-SM boundary).

### Function: HorseRoadFollow_PushPathB (0x180A4E39C)
- Appends road-point ids from (sample.m_pFrom, m_pTo) into m_pathB (int32 vector) via 0x180A0B38C; caps history to the last 10 entries.
- m_pathB = backtrack history.

### Function: HorseRoadFollow_UpdateTurnParams (0x180A4DFF4)
- Builds a vector from sample.m_along (plus a seeded {3}); runs RoadSnap_TestVector; on success sets **m_fastStop (+0x10B) = 1** (sharp-corner slowdown flag).

### Function: PathHistory_PruneOlderThan (0x181ECA180)
- lower_bound over 8-byte {flag,time} records: first index with `record.time >= now - window`.

### Function: HorseRoadFollow_RebuildControllerMode (0x180A4E9B8)
- Mode source: options object (FUN_1804aade0) + 0xB0, clamped to 0..2; forced 0 when the horse move-adapter chain is invalid (0x1806CCCD4/0x1804A8CE0).
- On mode change: scalar-dtor the old controller, set m_mode (+0x70), then create:
  - mode 1 -> S_OnPressController_Factory(&slot, S_HorseData*, FUN_181302a54(moveAdapter));
  - mode 2 -> S_AutoController_Factory(&slot, S_HorseData*);
  - mode 0 -> none.
- m_pMagnetism (+0x68) = *slot. For the on-foot adapter, force mode 2 (auto) — the foot player has no rider adapter for mode 1.

### Function: PathHistory_FlickScore (0x181ECA440)
- `int PathHistory_FlickScore(AW_S_AutoController* c)` = RoadState_GetIndex(horse+0x7A0, HorseData_GetTag(horse)); caller rejects when result == 4 (off-road state).

### Function: Road_SnapChooser (0x181EC9D50)
- `bool Road_SnapChooser(const int* states, AW_S_AutoController* c)`: true when `S_HorseData+0x30` (m_horseState, E_HorseState) equals one of the 4 bytes of the caller's constant 0x060B0A00, i.e. states {0x00, 0x0A, 0x0B, 0x06}. Gate for snap acceptance.

### Function: RoadSnap_TestVector (0x180A4E208) / RoadCart_TestPoints (0x180A4E2EC)
- RoadSnap_TestVector builds a lambda capturing `frame+0x9C/+0xA0/+0xA4` (RoadMagnetismCartWidth/Height/CenterOffsetY, scaled by 0x18409A210) and calls RoadCart_TestPoints over the vector of points/entities.
- RoadCart_TestPoints iterates an entity container (whGlobal+400 -> vf[0x70](id) per id; 0x38-byte records) and invokes the lambda per entity — the cart-box proximity test.

### Function: RoadPath_AppendPointId (0x180A0B38C)
- Appends road point ids (S_HorseRoadPoint.m_id, +0x28) into an int32 vector (m_pathB) with dedup: skips when last == from-id; pushes to-id after from-id; avoids consecutive duplicates.

## Phase C — S_OnPressController (hold-E mode)

### Layout
0x30: magnetism vptr @0, rider-SM-modifier vptr @8, m_pPlayer @0x10,
m_pHorseData @0x18, m_deactivateTime @0x20, m_reactivateTime @0x24,
m_hintTime @0x28, m_flags @0x2C (bit0 active, bit1 latched, bit3 flick,
bit4 interrupted/armed).

### Function: S_OnPressController_SetHoldLatched (0x180A4E98C)
`m_flags = (m_flags & ~2) | (latched << 1)` — trivial bit update.

### Function: S_OnPressController_Tick (0x180A4E768)
- Phase 0: if armed (bit4): rider input via `m_pMove(+0x100)->vf[0x18]()`; yaw below const OR move below 0.2 -> clear bit4 (interruption clears the armed state). Chat-follow active -> clear active/latch bits + zero timers (deactivate).
- Phase 1: countdown m_deactivateTime and m_hintTime; every 10th frame sets C_Player+0xB08+0x110 (hint visibility) = 1; if horseData+0x10C (jump request) -> clear active/latch bits.
- Phase 2: reset m_deactivateTime; if `|riderYaw|*deg < frame+0x88` (RemainAngle) OR move <= 0.2 -> stay; else set bit4 (armed) and copy frame+0x204/0x208 (DeactivateTime/ReactivateTime) into m_deactivateTime/m_reactivateTime.
- Phase 3: countdown m_deactivateTime; when exhausted count down m_reactivateTime; at 0 -> clear active/latch bits and timers (deactivate after the reactivate grace window).
- Phase 5: if sample.m_failed == 0 return; else clear bit0, set bit3 (flick), zero timers.
- Semantics: rider input beyond RemainAngle arms deactivation timers; the follow survives a grace period (ReactivateTime) before deactivating — exactly the "manual input interruption/falloff" behavior. Jump request and chat-follow deactivate immediately.

### Function: S_OnPressController_HintsActive (0x180A4E99C)
`(m_flags & 1) && m_deactivateTime <= 0 && !(m_flags & 0x10)` — the hold-E hint visibility predicate.

### Function: S_OnPressController_Factory (0x181932168)
Allocates 0x30 and calls S_OnPressController_Ctor(obj, S_HorseData*, moveAdapter) (ctor 0x1819321F8).

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
