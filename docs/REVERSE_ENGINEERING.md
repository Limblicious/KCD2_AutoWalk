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
- m_pMagnetism (+0x68) = *slot. The user's requested horseback-style hold-E UX maps to mode 1 (S_OnPressController); the foot port must reproduce/adapt mode 1 rather than force mode 2.

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

### Function: S_OnPressController vtable slot 1 (0x180A4E98C) — RESOLVED
Call-site ABI: `bool slot1(this, latched, hit, sample*)`.

Disassembly shows that the OnPress implementation ignores `latched` and
`sample`, clears bit1, writes bit1 from `hit`, then tail-jumps to
`S_OnPressController_HintsActive`.

Therefore:
- bit1 is NOT the persistent top-level latch; it mirrors current sample hit;
- persistent road-follow latch remains `S_HorseRoadFollow::m_latched`;
- the return value is the HintsActive/active-allowed predicate;
- the stale one-argument public-header declaration is invalid for this vtable
  slot.

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

## Phase E — Steering output chain (partial)

### Function: ApplySMOutput (0x180A4EF98, REL 56418)
- Recovered signature: `char ApplySMOutput(C_RiderPlayerInput* self, S_RiderSMOutput* smOut, S_RiderMoveRequest* request, S_HorseData* data, float dt)`.
- dt clamped to `frame+0x16C` (ClampDelta).
- smOut bits 0x2C route into a look-steer pair (FUN_181ECAF40 -> FUN_1829F2324(self, data, ...)) — the yaw/look application path.
- Road class selection: smOut[0] flags (0x8 branch checks FUN_1829F1F9C "magnetism active"), then RoadState_GetIndex(C_Horse+0x7A0, &class) -> request+0x10, plus the global road-record lookup (FUN_180648438()->vf[0x40], clamped by vf[0x50]) -> request+0xC.
- Request flags: request+4 = smOut[0]>>3&1; smOut bits 0x10/0x20/0x40 -> request+2/+3/+5; smOut[1]&1 -> request+6.
- Final speed scalar: `request+8 = dt * frame+0x170 (RotationCoeff) * clamp(self+0xA0C * self+0xA08) * const`.
- This is the exact vanilla boundary where the road command becomes horse movement; the foot adapter must consume the equivalent upstream value (m_magnetYaw -> view application) rather than this horse-SM request.

### Steering command chain (established)
`S_HorseRoadFollow::Tick` -> sample.m_yawFrom -> HorseRoadFollow_PublishMagnetism writes `S_HorseData.m_magnetYaw (+0x130)`. m_magnetYaw is a **yaw command, not world heading**; m_yawSmoothed (+0x128) is a SmoothCD state chasing it via HorseYaw_SmoothCD (0x18059B800, REL 31821); the sign-flip/pseudo-speed reset lives at 0x180A4FDF1. The mounted view consumes the smoothed yaw through the rider view-state glue (REL 37998) and camera centering (REL 56442) — still to be decompiled (Phase E/F continuation).

### Function: HorseYaw_SmoothCD (0x18059B800, REL 31821)
- Recovered signature: `void HorseYaw_SmoothCD(S_HorseData* data, void* out, float dt)`.
- Full vanilla yaw pipeline:
  1. target = m_magnetYaw (+0x130); if bridle-state check (FUN_18059BAD8(C_Horse)) fails -> target = 0.
  2. dt clamped to frame+0x16C (ClampDelta).
  3. **Manual blend**: if m_pMove (+0x100) present, `target = m_pMove->vf[1](target, data)` — rider input modifies the commanded yaw (Phase D entry point).
  4. Sign flip of target vs m_yawSmoothed -> reset m_yawSmoothed = 0.
  5. If |wrap(target-current)| > epsilon (0x18409A4A8): SmoothCD step with
     omega = dt * frame+0xE8 (RotationMax); critically-damped coefficients
     (0x18409A2D8 / 0x184099F44 / 0x18409EE60 / 0x18409EE64); in/out speeds
     frame+0xEC (RotationSmoothOutSpeed, when m_magnetYaw==0) vs frame+0xF0
     (RotationSmoothInSpeed); step clamped to +/-omega; writes m_yawSmoothed
     (+0x128) and m_yawVel (+0x12C).
   6. Output: FUN_18059BBF8(out, {0, m_yawSmoothed}); then via
      [C_Horse+0x9E8]->vf[8]() rider-sync object -> vf[0x40](&{0, m_yawSmoothed},
      m_magnetismLive) — the exact downstream application boundary (horse yaw
      into the rider view/sync, gated by magnetism-live).
- Foot adapter: reproduce this exact smoothing (same cvars), but do NOT route
  the road command through Henry's view channel. The physical body/travel
  consumer is being recovered separately; camera/view ownership is handled
  through the native FocusCamera mechanism.

### Function: I_HorseRiderSync::PushRiderAction (0x18059BC40) — Gate A
- Recovered signature: `void PushRiderAction(void* sync, const Quat* in, bool skip)`.
- Gate: runs only while `*(sync+0x18) & 1` (sync active flag) and !skip.
- `FUN_18059BBF8(request, quat)` converts the quat into the movement request's
  turn accumulator at **request+0x88** (a Vec3): on the request's first use
  (flag bit 0x40 clear) it OVERWRITES the vec with the quat xyz; on subsequent
  updates before consumption it ADDS (accumulates) the quat xyz.
- The request then flows to the consumer: `*(sync+0x28)` (= the
  C_RiderPlayerControl) -> vf[0x228] (slot 69) -> object -> vf[8](object,
  request) — the horse movement-request applier.
- **Semantics established**: `{0, m_yawSmoothed}` is a yaw-only "quat" whose
  **y = the per-update turn amount** (a delta/rate-limited command, NOT a
  heading). The consumer applies the accumulated value to the horse body each
  update; the per-frame rate limit lives inside the CD step itself
  (omega = dt * RotationMax), so the downstream applies the smoothed value
  DIRECTLY with no further dt multiplication.
- Consequence for the foot port: the travel frame must integrate
  `travelYaw += smoothed` per update (no dt factor), and the CD target must be
  a relative turn command (the port computes `WrapPi(roadDir - travelYaw)`);
  the sampler's yawFrom is the facing-normalized world direction of the
  from->to road segment acting as the command target.

### Function: HorseSM_ApplyLookSteer (0x1829F2324)
- The smOut 0x2C branch: anim-state triggers ("HRAC_KUN_POBIDKY"/"HRAC_SPURRING"),
  horseData+0x10A (m_scared) on spurring, anim-bind tokens on +0x448
  (FUN_180CFA924 9/2), rider-input flags (FUN_1829F22E8 / FUN_1829F2420).
- Horse animation/bridle path; the steering yaw itself flows through
  HorseYaw_SmoothCD, not here.

## Phase F — Mounted camera / view seam

### Function: HorseFlatYaw_To_RiderLookAccum (0x1806CCAF8, REL 37998)
- `void HorseFlatYaw_To_RiderLookAccum(C_Actor* actor, const Quat* horseDelta)`.
- Reads C_ActorPhysicsState at actor+0x238; builds the flat-yaw quat from
  physicsState+0x34, quat-multiplies with the horse delta quat, converts to
  euler, and **additively accumulates the yaw into physicsState+0x88
  (m_lookAngleAccum)**. Then stores the quat back (FUN_1806442A8).
- This is the mounted camera-decoupling seam: horse yaw flows into the actor
  look accumulator, independent of the mouse-look request.

### Function: C_ActorPhysicsState_Tick (0x1804415E0, REL 26156)
- Advances transient/carried look deltas, ingests the look request
  (param_2+0x18/0x20 -> +0x7C pending), applies limits (FUN_180441928), and
  **zeroes m_lookAngleAccum (+0x88) at the end of every tick** (after the
  camera compose has consumed it).
- Frame order: glue adds yaw -> compose consumes -> tick zeroes.

### Function: MountedCamera_Centering (0x180A501E8, REL 56442)
- Camera recenter state machine on the rider camera object: builds the
  centered quat, SmoothCD (frame+0x100 CameraCentering, +0x104/+0x108
  CenteringTime/InCombat, CD constants 0x18409A490/0xA44C), and adds the
  recenter yaw into the same m_lookAngleAccum. Wide/narrow view-limit
  selection is road-index driven (param_3[0x24] vs RoadState_GetIndex);
  view-limit values come from frame+0x158/0x15C/0x160
  (ViewLimitWide/Narrow/Bottom), clamp via ActorViewLimit_Clamp (0x18053B508,
  REL 30673).

#### Mounted centering input, ownership, and blend (branch-complete)
- The caller is `C_RiderPlayerInput::Update` (0x180A4EAB4). It passes
  `C_RiderStateMachine*` (`input+0x30`), `C_RiderPlayerInput::m_look`
  (`input+0x9CC`), `S_HorseData*`, an allow flag, and native frame dt.
- `I_MovementInputListener::OnMovementInput` (0x18059BB20) copies the game's
  look vector directly into `m_look`. Centering treats `m_look.x != 0` or
  `m_look.z != 0` as player look input and calls the reset helper
  (0x180A51058) every such frame.
- The reset helper clears `m_centerBlend (+0x910)`, the two SmoothCD velocity
  terms, and the magnetism transition byte; it captures the current native
  center values and current timer time. There is no guessed mouse timer in
  the ordinary branch.
- `C_Player+0xCF0` is the FocusCamera object. The routine reads
  `FocusCamera::m_flags bit 1`; while FocusCamera is active, mounted
  centering does not write the actor look accumulator. FocusCamera is
  therefore a competing camera owner, not the mounted recenter mechanism.
- With no look input, the target quaternion is
  `horseEntityRotation * relativeYaw * pitch(CameraCenteringPitchOffset +
  smoothedHorsePitch)`. Magnetism may add a clamped relative-yaw term; its
  absolute body frame still comes from the horse entity rotation.
- Blend-in is exact: `m_centerBlend += CameraCentering * dt`, clamp to 1,
  then slerp from the current actor view quaternion to the target by that
  cumulative blend. The routine converts `inverse(current) * target` to
  `Ang3` and additively writes all three components to
  `C_ActorPhysicsState::m_lookAngleAccum`.
- `C_ActorPhysicsState::Tick` consumes the accumulator later in the same
  pre-physics update and clears it. `C_CameraRider::Compose` is downstream
  output smoothing; it is not the recenter state machine.

#### On-foot adaptation decision
- Use the same pre-physics movement-request seam already hooked for travel:
  its request `+0x18 Ang3` is copied immediately afterward to
  `m_lookDeltaRequest`, so x/z are the on-foot equivalents of mounted
  `m_look.x/.z`.
- Replace only the horse entity's absolute yaw with the independently
  recovered `g_travelYaw`. Keep the native CameraCentering rate,
  CameraCenteringPitchOffset, cumulative slerp, quaternion delta, and
  `m_lookAngleAccum` output channel.
- Reset on manual look, autonomous-follow cleanup, UI ownership, and active
  FocusCamera. Do not install a synthetic FocusCamera provider: its target
  focus/limit lifecycle is orthogonal and the mounted routine explicitly
  yields to it.

#### Native target-term accounting (foot port, 2026-10-09)
Every term of the native centered-target construction, and how the foot port
handles it:

| Native term | Native source | Foot handling | Status |
|---|---|---|---|
| Absolute body frame | horse entity rotation (`S_HorseData+0xF8 -> vf0x180`) | `WrapPi(m_flatYaw + g_travelYaw)` — the travel command is flat-relative (see below), so the world travel heading is the sum | REPLACED — **runtime-verified** (three-condition experiment, prediction B within ~2 deg) |
| Relative yaw | mounted rig relative angle | omitted (rig-specific, no on-foot analog) | OMITTED |
| Road-magnetism yaw | `RoadState_GetIndex` vs `param_3[0x24]` + `MagnetismDegreeLimit` clamp | omitted: the road-follow yaw already enters through `g_travelYaw` (which chases the native road sampler output); a separate magnetism-yaw term would double-count | OMITTED (documented rationale) |
| Heading SmoothCD | state `+0x918/+0x91C` chasing `FUN_180a50dcc` (degrees) | omitted: that smoothing feeds the native pitch composition (`pitchOffset + smoothed`); on foot there is no recovered analog source | OMITTED — native parity claim does NOT include this term |
| Pitch | `CameraCenteringPitchOffset` (+0x110) | ported verbatim | PORTED |
| Restart delay | `CameraCenteringTime` (+0x104) / `CameraCenteringTimeInCombat` (+0x108) | normal +0x104 only; combat variant not selected on foot | PARTIAL |
| Blend rate | `CameraCentering` (+0x100) | ported verbatim | PORTED |
| Blend curve | `inverse(currentView) * slerp(currentView, target, blend)` | ported verbatim (`CameraRecenterMath.h`) | PORTED |
| Output channel | `C_ActorPhysicsState::m_lookAngleAccum` (all 3 Euler components) | ported verbatim | PORTED |
| FocusCamera ownership | yield while `FocusCamera flags & 2` | ported | PORTED |
| Look-input reset | every frame with `m_look.x/.z != 0` calls the reset helper `0x180A51058` | foot gate is the look-axis EVENT stream (`NotifyMouseLook`), not the processed look vector — raw mouse/XI events are the closest on-foot analog of the mounted look vector; event-shape verification is in the runtime log (`input: look event received`) | ADAPTED — event mapping pending runtime confirmation |

Honest parity statement: the target frame (travelYaw) substitution and the
raw-event interruption gate are the two adaptation points that remain
runtime-unverified; the blend/delay/delta/output math is a verbatim port.

#### Timer lifecycle (foot port vs the native reset helper)
- Native: the reset helper `0x180A51058` runs on every look-input frame and
  on the disallowed path; its exact field writes (whether it re-stamps the
  `+0x914` timer) were established from the caller flow, not from a
  branch-complete decompile of the helper body.
- Foot port: `NotifyMouseLook` re-stamps `g_lastLookMs` (the look-event
  analog); `Reset()` clears only the blend and does NOT re-stamp, matching
  the verified caller flow (the helper is invoked on look frames, not on
  arbitrary state resets). UI-open/disengage are foot-specific resets with
  no native stamping analog.
- Unresolved until `0x180A51058` is decompiled: whether an
  engagement/menu-close reset should also restart the delay. Current choice
  (no stamp) is the conservative reading of the verified flow.

#### Reference-frame correction (2026-10-10, runtime-verified)
The three-condition experiment (locked recenter / view held ~90 deg /
looking down the path) established that the whole on-foot road-follow
pipeline is **flat-yaw-relative**, not world-space:

- `moveYaw ≈ WrapPi(flatYaw + requestYaw)` in every condition (error
  within ~2 deg; the competing `move ≈ request` prediction fails by
  68-162 deg). `flatYaw` = `C_ActorPhysicsState::m_flatYaw` (+0x44), the
  view-derived body-facing reference; the entity world-TM rotation and the
  rendered camera track it exactly.
- The road sampler's `yawFrom` (`m_magnetYaw`) is facing-normalized: it
  returns the road heading relative to the flat yaw, so
  `flatYaw + magnetYaw` reproduces the world road heading (0.2-4.9 deg
  agreement with the measured displacement).
- The movement request consumer therefore applies the request velocity in
  the flat-yaw frame; the vanilla "world-rotated" field comment described
  the PRODUCER (view yaw + local input), not the consumer contract.
- Consequence: `g_travelYaw` is a flat-relative command. The movement
  (flat + travel) was correct; only the camera target misused `travelYaw`
  as a world heading. The recenter target is now
  `WrapPi(m_flatYaw + g_travelYaw)`.
- Feedback note: `magnetYaw = R - flatYaw` cancels the flat term in
  `flat + travel`, so the composed target tracks the road heading R while
  the camera moves; a residual transient exists while `travelYaw`
  re-converges after fast view rotation. The runtime `tgtDeltaDps` /
  `tgtErrCam` diagnostics verify stability.
- **Pull-rate cap (2026-10-10, runtime-required)**: the first deployed
  correction applied the full remaining error per frame once the blend
  reached 1; because the flat yaw follows the view, the composed target
  retreats at the pull rate and the error never shrank -- runaway spin.
  The native never applies a full-error pull (its target is view-independent
  and its pull is blend/heading-stage rate-limited). The port now caps the
  per-frame pull at the native CameraCentering rate (`ClampDeltaStep`,
  `CameraRecenterMath.h`), keeping the pull below the road-follow
  compensation rate so the loop converges monotonically. The omitted native
  heading SmoothCD stage remains approximated by this cap, not by the
  verbatim stage.
- **Dead-zone (2026-10-10, runtime-required)**: with the rate cap the loop
  converged but sustained a ~1-2 deg limit cycle (camera bobble -> flat ->
  sampler facing-normalization -> travel smoother with its native sign-flip
  reset -> target bobble). The port skips the pull while the remaining error
  is below 2 deg (`InsideDeadZone`), letting the coupled loop settle. The
  native needs no zone because its target is view-independent.
- **Proportional pull (2026-10-10, requested feel)**: the constant-rate cap
  and the 2-deg dead-zone produced a slow linear swing and a visible step
  when the target moved through curves. The pull is now a proportional
  velocity controller (`PullVelocity`):
  `velocity = sign(e) * min(gain * max(|e| - d, 0), maxRate)` -- the
  dead-zone d (0.75 deg) is subtracted from the error so the velocity
  approaches zero continuously at the boundary (2026-10-10 review
  correction). gain and the cap are fractions of the native road-follow
  smoother rate (`rotationMax`), keeping the pull below the compensation
  rate (the runtime-established stability constraint). The native blend
  ramp is intentionally bypassed: rotation begins at the proportional
  velocity when the CameraCenteringTime delay expires; `g_centerBlend`
  remains only as the delay-gate state and no longer scales the pull.
  Startup behavior is therefore a faster swing than the mounted algorithm,
  per the requested feel.

#### Input-listener registration (2026-10-09 fix)
`EnsureInputListener()` was previously reachable only through
`FollowController::Enable()` (the console command); normal hold-E play never
registered the look-input monitor, so the recenter could never be
interrupted. It is now ensured at the top of every `Tick()` with
once-only registration, per-tick retry until the input system is available,
rate-limited failure logging, and a fail-closed gate: while the monitor is
unregistered the recenter never applies the view pull.

## View/body separation seam (recovered — the body-facing mechanism)

The player's C_ActorPhysicsState keeps the VIEW and the BODY flat yaw as
separate states, with a scoped hold that decouples them:

### Function: SetViewRotation (0x1806440AC)
- Sets the view: normalizes the input quat, writes m_lookQuat (+0x14) and
  m_lookAngles (+0x08, QuatToAng), and m_viewPitch (+0x48).
- **Gate**: only when BOTH hold counters (owner+0x174 and owner+0x178) are
  < 1 does it rebuild m_flatYawQuat (+0x34) as a yaw-only quat of
  m_lookAngles.z and refresh m_flatYaw (+0x44). While either counter is
  positive, the flat yaw (the body's facing reference) stays under its own
  owner and the view rotates freely -- the game's "free look" scoped hold.

### Function: SetFlatYaw (0x1806442A8)
- Writes m_flatYawQuat (+0x34) from the input quat; rebuilds m_lookQuat from
  the flat yaw rotated by the current m_viewPitch (the view follows the body
  while preserving the view pitch); rebuilds m_lookAngles from the flat quat
  only when owner+0x174 < 1; refreshes m_flatYaw (+0x44).

### Hold counters (ref-counted, scoped)
- vf135 (0x18040B580): `owner+0x174 += (inc ? +1 : -1)`.
- vf136 (0x18286139C): `owner+0x178 += (inc ? +1 : -1)`.

### Foot-port consequence — superseded by FocusCamera recovery
Do not implement a manual `+0x174 + SetFlatYaw` scheme from this section.
Subsequent RE identified `C_FocusCamera` as the native owner of the +0x174
scope hold, captured flat-view reference, target-relative tracking and limits.
The remaining body/root consumer still must be recovered separately.

### vf13 flat-yaw consumption (proof of the body-facing link, 2026-10-07)
C_ActorMovementController::vf13 (0x1804B8E88) reads BOTH view and body state
in its look-steer construction:

- `FUN_180441b14` (the branch gate) is **the hold counter itself**:
  `return *(int*)(actor+0x174) < 1`.
- Branch A (counter HELD, cVar7 == 0): builds the look-turn from
  state+0x14 (m_lookQuat — the view).
- Branch B (counter clear + global byte DAT_18492da39 != 0): builds it from
  state+0x34 (m_flatYawQuat) + state+0x40 (flat quat w).
- Branch C (counter clear + velocity magnitude > eps + FUN_1804ace9c==0):
  builds it from state+0x34 (m_flatYawQuat) + state+0x40, with the CD
  turn-smoothing constants (0x18409EE60/0x18409EE64).
- The branches write param_3[6]/param_3[8] = S_MountAnimState
  m_deltaAngles x/z (the per-frame look-turn), and m_rootRotation
  (out+0x3C) = the controller's internal body quat (controller+0x11C).

Conclusion: m_flatYawQuat IS a movement-request reference on foot — the
normal (counter-clear) path steers against the flat yaw, and the held path
steers against the view. The inversion vs the initial hypothesis (held ->
view-referenced, not flat-referenced) is the RAW observation; the exact
activation semantics (global byte, FUN_1804ace9c) and the runtime behavior of
a scoped hold + SetFlatYaw still require a live validation before
implementation. Do not implement the hold+SetFlatYaw design until a runtime
test confirms which state the body faces under the hold.

### Camera architecture consequence
The mounted free-look mechanism is now identified as **C_FocusCamera ownership**,
not merely "increment a hold counter":

1. C_FocusCamera::Activate owns +0x174 via vf135.
2. It captures the activation flat-view reference.
3. Its tracker maintains a target frame (horse on the mounted rig).
4. Update applies target-relative view rotation/limits through SetViewRotation.
5. C_FocusCamera::Deactivate releases the hold and restores camera state.

For the foot port, prefer reusing this native mechanism if a safe target provider
can be supplied for Henry's independently controlled body frame. Do not manually
toggle +0x174 and invent limit math until C_FocusCamera::ShouldBeActive and the
target-provider requirements are fully recovered.

Open questions before implementation:
- can a provider with EntityId 0 / direct target frame remain active, or does
  ShouldBeActive require a distinct target entity;
- what exact limit pairs/mode/stiffness the mounted FocusCameraNode installs;
- whether the mounted rig's setup can be cloned/adapted without a live horse.

### Hold-counter callers -- IDENTIFIED: C_FocusCamera (the mounted free-look)
The player's vf135 (0x18040B580, the +0x174 hold) is held and released by the
C_FocusCamera (C_Player+0xCF0, header game/C_FocusCamera.h):

- Activate (0x1808B9DB8): calls the player vtable slot 0x438 (= vf135) with
  1 -- the hold INCREMENT -- for BOTH mode-0 and mode-2 setups. Mode-2 also
  captures `m_capturedFlatView` (+0x58) = view-state +0x34/+0x3C (the
  flat-yaw quat) at activation.
- Deactivate (0x1808B8A04): calls vf135 with 0 -- the hold RELEASE -- for
  mode-0 and mode-2, and restores the camera manager FOV-blend fields.
- The tick (Update 0x1808BA014) pulls the view toward the target frame (the
  horse, via the Apply step 0x1808B8BFC: view-state +0x24 quat rotated toward
  the target Matrix34, step min(dt*100,1)/(stiffness+1), math 0x1808B8C5C)
  and clamps TARGET-RELATIVE via SetViewRotation (0x1806440AC) -- the
  horse-relative ViewLimit pinning. The mounted A/D follow itself is the
  first-person compose's world-yaw mix, not this system.

The mounted rig = a C_FocusCameraNode setup (entity-id provider
sub_1827D7E80, target entity part/slot 9 = the horse). The mechanism:
hold +0x174 (scoped free-look) + flat-view capture + target-relative limits
+ the view stays mouse-driven while the horse body turns through its own SM.

### C_FocusCamera::ShouldBeActive (0x1808B9B78 -> core 0x180B26A00)
- Resolves the setup's target provider to the entity (via the entity
  manager) and runs the activation gates:
  1. Player-check chain (FUN_180B26BF0/FUN_18285D888) then compares the
     target's +0x668 road-cache object against the player's -- EQUAL =>
     inactive: **the setup rejects the player's own horse as the target**.
  2. `*(target+0x250)+0x18` flag => inactive.
  3. Game-state gates: whGlobal+0xF8 chain (FUN_180B26BA4), a dialogue
     check, the player action state (slot 0x5e), camera checks
     (FUN_1809E54A4 slot 0x60), and the actor-model flag (slot 0x1b,
     0x18040B580-adjacent) -- all must be clear.
- Foot-port viability: a provider with GetEntityId() == 0 skips the
  target/entity checks entirely (the header: "GetEntityId may return 0") and
  falls through to the game-state gates -- so a custom zero-id provider (or a
  provider bound to a non-player frame) can activate the native FocusCamera
  machinery on foot, subject to those remaining state gates. The target
  frame/limit-pair/align-speed/stiffness live in the tracker (+0x38) filled
  by the setup's provider chain -- the exact fill to recover next.

### Controller mode for the hold-E interaction -- SETTLED: mode 1 (S_OnPressController)
- RebuildControllerMode (0x180A4E9B8) selects: mode 1 -> S_OnPressController_Factory
  (the hold-E controller with the hint rows); mode 2 -> S_AutoController_Factory
  (the no-hold automatic follow).
- The hold-E UX (horse_magnetism_activate/deactivate hints, the latched bit,
  the interruption timers) IS the S_OnPressController: its SetHoldLatched is
  bit-trivial (0x180A4E98C: flags = (flags & ~2) | (latched << 1)) and its
  tick runs phases 0-5.
- The AUTO controller's SetHoldLatchedImpl acceptance gates (enter/remain
  angles, chat gate, snap/flick history) belong to mode 2 ONLY. The port
  MUST NOT hybridize: the mode-1 port needs the OnPress phases and the tick
  flow, not the auto acceptance gates or the auto path history.
- Runtime confirmation: the user's live option (FUN_1804AADE0()+0xB0)
  logged 1 in the earlier session; the on-disk .data default is 0 (the real
  default loads from the profile registration).

### OnPress slot-1 ABI -- RESOLVED (corrects the stale header)
S_OnPressController vtable slot 1 (0x180A4E98C), disassembly-verified against
the S_HorseRoadFollow::Tick call site (ctrl, latched, hit, &sample):
```
and  byte [rcx+0x2c], 0xfd      ; flags &= ~2
add  r8b, r8b                   ; r8b = hit*2  (R8 = the hit arg)
or   byte [rcx+0x2c], r8b       ; bit1 = hit
jmp  HintsActive                ; relocated tail-jump (rel32 zeroed on disk)
```
- The function reads ONLY the hit (r8); the latched (rdx) and the sample
  (r9) arguments are UNUSED -- the tick's 4-arg call is the interface, the
  implementation consumes one of them.
- The return (AL) is the tail-called `HintsActive` result, not void.
- Semantics: bit1 mirrors the current sample hit; the return is the controller's
  current active/allowed predicate used by the top-level tick.
- IMPORTANT: older notes reconstruct `HintsActive` as including more than
  bit0 (deactivate-time / armed-state checks). Re-decompile 0x180A4E99C and
  settle the exact predicate before implementing mode 1; do not simplify it
  to `flags & 1` unless the binary proves that. The old public header
  "void SetHoldLatched(bool latched)" is WRONG for this slot.
- The tick's flow around it: slot3 GetRoadDistance -> wrapper sample ->
  slot2 Tick(phase 0) -> slot1 (bit1=hit, returns active) ->
  if !hit || !active: release (latched=0, phase 3); else if !latched:
  Tick(4) (enter -> bit0), latched=1, Tick(2), UpdateTurnParams,
  PushPathB, Publish; finally Tick(1).

### Remaining mode-1 activation gates before port rewrite

Two native pieces remain required before the mode-1 controller can be called
branch-complete:

1. **Phase 4** of `S_OnPressController::Tick`.
   `S_HorseRoadFollow::Tick` calls phase 4 on the transition from
   `m_latched == false` to a successful first follow tick. The current phase
   table omits phase 4 entirely. Recover its exact reads/writes/timers/flags.

2. **The native hold-E action -> bit0 active setter**.
   Slot 1 is evaluated before phase 4 and returns the active/allowed predicate,
   so bit0 must already be established by another native path when the rider
   requests magnetism. Trace the real `horse_magnetism_activate/deactivate`
   action/listener/state-machine path into `S_OnPressController::m_flags bit0`.

Until both are recovered, do not emulate activation by directly setting bit0 in
the foot port. The existing custom foot prompt/input can trigger the port only
after we know what native state transition it is supposed to reproduce.

### OnPress phases for the mode-1 port (branch-by-branch mapping)
Exact decompile of S_OnPressController_Tick (0x180A4E768); fields +0x20
deactivate, +0x24 reactivate, +0x28 hintTime, +0x2C flags (bit0 active,
bit1 = the sample HIT mirror [see slot-1 ABI], bit2 = second engage flag
[cleared together with bit0 by phase 3 and the chat/jump path -- role
exact], bit3 flick, bit4 armed):
- Phase 0 (armed): rider stick (m_pMove->vf[0x18] -> turn/move) -- if
  turn < DAT_1840B8640 OR move < DAT_18409EE58 -> clear bit4 (the
  interruption clears on return-to-neutral). Then the chat-follow check:
  [whGlobal -> player +0xCE8]->vf[8]() -> cVar3.
- Phase 1: countdown +0x20 and +0x28; every 10th frame (frame-counter
  %10==0) set C_Player+0xB08+0x110 = 1; jump request (horseData+0x10C)
  -> cVar3.
- Phase 2: reset +0x20 = 0; the rider input: (turn & mask) * const <
  frame+0x88 (RemainAngle) -> stay (return); move <= DAT_184099F88 ->
  stay; else set bit4 + copy frame+0x204/+0x208 into +0x20/+0x24.
- Phase 3: if +0x20 <= 0: +0x24 -= dt (clamp 0); if +0x24 > 0 -> return;
  else clear 0xFA (bits 0 AND 2) + zero +0x20/+0x24.
- Phase 4: **NO-OP** -- the dispatch falls through `if (param_3 != 5)
  return;`. The "enter" is NOT a tick phase.
- Phase 5: if sample.m_failed == 0 -> return; else clear bit0 (0xFE),
  set bit3 (flick), zero the timers.
- After phases 0/1: cVar3 (chat-follow or jump) -> clear 0xFA + zero
  the timers.
- The bit0 (active) setter lives in the hold-E ACTION path (the ctor's
  registered functor -- engage sets bit0, the deactivate hint clears it);
  the functor's invoke is interfuscator-runtime-patched, so its exact
  body is unverifiable statically -- the tick's slot-1 predicate
  (HintsActive) gates the follow on it. The port therefore models bit0
  as the action-controlled engage bit.
- Foot adaptation: the rider stick/move becomes Henry's WASD held state
  (no analog stick on foot); phase 2 arms when WASD is held beyond the
  tolerance, phase 0 clears the armed state when WASD returns neutral --
  brief WASD interruption/resume, sustained WASD deactivates via phase 3.

### HintsActive (0x180A4E99C) -- the exact predicate
`(flags & 1) && !(deactivateTime > 0) && !(flags & 0x10)` -- active AND
the deactivate timer at 0 AND not armed. This is the boolean the slot-1
tail-jump returns to the tick (cVar4), gating whether the follow proceeds.

### Mounted FocusCamera setup tracker fill (from Activate 0x1808B9DB8)
The tracker (C_FocusCamera+0x38) is filled on activation:
- +0x20/+0x28 (the target-relative limit pairs) = the setup's +0x1C/+0x28
  fields (only when the setup's +0x18/+0x24 validity bytes are set, and
  only when the value is <= the current tracker value).
- +0x30/+0x38 (the align speeds) = the tuning cvars FUN_1804AADE0()+0x13C/
  +0x140 and +0x144/+0x148.
- +0x40 (the stiffness) = the setup's +0x30 field.
- Mode-2 extra: captures m_capturedFlatView = view-state +0x34/+0x3C,
  arms the recenter timer (+0x48) from tuning +0x150, and the interp init
  (0x180A70610) from tuning +0x154.
- Deactivate restores the FOV-blend fields from tuning +0x15C and releases
  the +0x174 hold (vf135(0)).
### Mounted FocusCamera setup installer -- RECOVERED: C_FocusCameraNode
The mounted rig is a `wh::animationmodule::C_FocusCameraNode` (the concept-
graph node, vtable 0x183B75990; header animationmodule/C_FocusCameraNode.h),
instantiated by the riding rig's animation asset graph targeting the HORSE.
Its [43] InstallSetup (0x1827DBE94) builds an S_FocusCameraSetup and installs
it on the local player's C_FocusCamera (client actor+0xCF0):
- provider: the tag-9 WUID/entity-part provider (sub_1827D7E80, target
  entity part/slot 9 = the horse) or the direct-object fallback
  (sub_1827D7F28);
- setup+0x04 = the mode byte (port +0x188); setup+0x05 = 1 (node-installed,
  outranks the combat lock);
- setup+0x18/+0x24 = the Vec2 angle-limit pairs (ports +0x108/+0xC8);
- setup+0x30 = the stiffness (port +0x148);
- the removal id stored at node+0x24C.
The exact limit/stiffness/mode VALUES are asset-driven (the riding rig's
animation graph), not code constants -- a foot adapter supplies its own via
the same S_FocusCameraSetup + Install path, with a provider yielding
Henry's autonomous travel frame (the zero-id provider skips the target
rejection per ShouldBeActive).
The second installer in the image = the combat lock path (sub_1808B8548).

REMAINING Track A: the controller+0x11C -> m_rootRotation final body
consumer (animated character / entity root orientation update).

### Sampler facade field corrections (current typed hierarchy, 2026-10-07)
- C_Horse+0x668 = C_Actor::m_pSoul (NOT a "road cache"): the sampler's
  road-record lookup (0x1807FE804 -> 0x1807FE964) reads a sorted
  0x38-record store through it (fields +0xC98/+0xCA0/+0xCA8) -- the
  road-record manager keys the records by the actor's SOUL. The facade now
  points +0x668 at Henry's real soul (with the owned zeroed buffer only as
  the null fallback). The old "roadCache" naming was stale.
- C_Horse+0x7A0 = C_BoneSlotSystem (0x1E0, embedded): the "road class"
  reader (0x1808324C0) reads the int[4] at +0x188 = the bone-slot system's
  m_indicesA (init -1; the facade's zeroed indices read as 0). The two RE
  generations agree on the offsets; the semantic names differed.
- The minimum valid facade state per the corrected hierarchy: the entity
  (+0x38), the real soul (+0x668), the horse-data back-pointer (+0x9E8),
  and the road-follow/horse-data internal links. The +0x7A0 bone-slot
  region stays zeroed (indices 0 -- semantics unresolved upstream).

### vf13 call site confirmed (pre-physics update 0x181E7D6F0)
- The movement controller = actor+0x180 (param_1[0x30]); the call is
  `(**(code**)(*(longlong*)param_1[0x30] + 0x68))(param_1[0x30], uVar9, &local_4f8)`
  -- vtable slot 13 (0x68/8) with **(controller, frameTime, S_MountAnimState*
  out)** -- confirming the hook signature (void*, float, float*).
- The request (zeroed by 0x180A709E8) then flows to
  C_ActorPhysicsState_Tick (param_1[0x47] = the actor's physics state; the
  +0x18 look delta -> m_lookDeltaRequest) and to the look/aim IK vf250
  (0x180640950, consuming the +0x24/+0x30 look/aim targets via
  0x180E7C42C).
- m_rootRotation (+0x3C, the copy of controller+0x11C) is NOT consumed by
  either of the two identified readers in the decompiled paths; its final
  consumer (the animated character root orientation) is still unconfirmed
  ("MED" in the header). The +0x11C writer set enumeration remains open.

### Downstream actuator chain status (Gate A — incomplete by design)
PushRiderAction (0x18059BC40) is fully decompiled: gate `*(sync+0x18)&1`
&& !skip; FUN_18059BBF8 converts {0,m_yawSmoothed} into the movement
request's turn accumulator at request+0x88 (overwrite on first use — flag
0x40 — then accumulate per update). The consumer call is
`*(sync+0x28) -> vf[0x228] -> plVar1 -> vf[1](plVar1, request)`.
sync+0x28 = the ctor arg a2 = FUN_1809115e0(rider+0x58) — an interfuscator
dispatch; the on-disk vtables of the rider classes are rewritten at runtime
(C_RiderPlayerInput's vtable area reads as string data at the expected slot),
so the static chase stops there. Resolving the final horse hull/body-yaw
mutation requires a RUNTIME pointer walk from a live C_RiderSync
(debugger/KCSE-side), not more static vtable reads.
Semantics established without the last hop: the smoothed value is a
per-update turn AMOUNT accumulated by the consumer (not a heading); the
rate limit lives in the CD step (omega = dt*RotationMax).

### Foot-adapter seam (superseded / do not implement)
An earlier proposal added road steering to Henry's m_lookAngleAccum (+0x88).
That channel is part of the VIEW integrator and is not the preferred autonomous
body-steering seam.

Current lead: use the scoped +0x174 hold to prevent view yaw from rebuilding
flat yaw, then update flat yaw independently. However, before implementation,
trace C_ActorMovementController / downstream body-orientation consumers to
prove that m_flatYaw/m_flatYawQuat actually drive Henry's physical body-facing
state rather than only a view reference.

## Phase G — Synthetic facade feasibility

Native dependencies enumerated from the recovered code:

| Dependency | Native usage | Foot handling |
|---|---|---|
| Road sampler (REL 194146 chain) | road acquisition | USE native (proven on facade) |
| S_HorseRoadFollow / S_HorseData state | controller state | synthetic facade (proven) |
| C_Horse+0x9E8 -> S_HorseData | back-pointer | set on facade |
| S_HorseData+0 -> I_HorseRiderSync (FUN_1806CCCD4 -> vf[1]) | move-adapter gate in SetHoldLatchedImpl | NOT fabricable safely (null crashes) -> port the gate in C++ |
| C_Horse+0x990 actor model (Horse_ModelQuery) | TickPhase1 gate | port in C++ (on foot: always false) |
| m_pMove (+0x100) -> vf[1]/vf[0x18] | manual yaw blend, rider input | port: Henry's own input instead |
| frame helper (REL 38017) cvars | all tuning constants | READ native at recovered offsets |
| C_ActorPhysicsState (+0x238) | look accumulator | USE Henry's own |
| chat-follow manager (C_Player+0xCE8) | engagement gate | USE Henry's real manager |

Conclusion: run the native road sampler + read native cvars; port the
controller/tick/state-machine logic faithfully in C++ (all branch-complete).
Do NOT apply road steering through Henry's look accumulator: that is a view
channel and is superseded by the recovered body/view ownership work below.
The native controller object itself is not required.

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


### Critical interface-slot ABI consistency check
`I_MagnetismController` public headers still describe slot 1 as
`SetHoldLatched(bool)`, but the recovered `S_HorseRoadFollow::Tick` calls
slot 1 with `(latched, hit, sample*)` and consumes a bool result. The
AutoController implementation has already proven the header was wrong for that
implementation.

Therefore the OnPress slot-1 function at `0x180A4E98C` must be re-decompiled
with the corrected call-site prototype before any mode-1 C++ port is considered
branch-faithful.


### Sampler facade layout WARNING — +0x668/+0x7A0 stale identification

Earlier sampler notes identify `C_Horse+0x668` and `C_Horse+0x7A0` as
road-state/cache dependencies. Current upstream typed layout contradicts that:

- `C_Actor+0x668 = C_Soul* m_pSoul`;
- `C_Actor+0x7A0 = C_BoneSlotSystem`.

Because C_Horse inherits C_Actor at base offset 0, the standalone facade's
`roadCache[0xCC0]` currently written to horse+0x668 is not type-correct.

Re-decompile REL 194136 / 55123 and all callees with the corrected actor/horse
types before making further claims about a horse road cache or tuning the
foot search radius around that assumption.
