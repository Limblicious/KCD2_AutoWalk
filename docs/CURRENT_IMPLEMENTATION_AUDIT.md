# Current Implementation Audit — 2026-10-07

This audit covers the pushed implementation through commit:

`1f00270cf365a9ece97b6cf5870bb79bdb405cf1`

It explains the observed failures:

- Henry initially arcs toward the road, then continues incorrectly/off-road;
- follow can release/stall after leaving the road edge;
- engagement can fail or produce no useful motion when Henry starts near the road edge/facing away;
- mouse look is not genuinely decoupled from travel like mounted riding.

## Executive conclusion

The current implementation is **not a faithful port of the recovered mounted controller**.

The Ghidra session recovered substantial native logic, but the final runtime code reverted after crashes from the attempted synthetic native controller. The safe fallback now:

1. calls only the road-sample wrapper;
2. manually publishes a few fields;
3. applies a hand-built smoother/acceptance layer;
4. rotates a camera-relative synthetic-W movement vector;
5. manually counter-rotates first-person look state.

This is why the feature behaves unlike mounted road following.

Do not tune the present steering constants. Replace the architecture described below.

---

## Finding 1 — `TickNativeRoadFollow` does not execute the native road-follow tick

Current `FootRoad::TickNativeRoadFollow` does **not** call REL 56405
(`S_HorseRoadFollow::Tick`).

It calls only:

`RoadSampleWrapper` / REL 194146

and then manually does approximately:

```cpp
if (hit) {
    rf->m_latched = 1;
    hd->m_magnetismLive = 1;
    hd->m_magnetHit = sample.m_hit;
    hd->m_magnetYaw = sample.m_yawFrom;
}
```

The complete recovered native tick also performs:

- controller `GetRoadDistance(latched)`;
- controller phase 0;
- `SetHoldLatched` acceptance gates;
- failure/exit phases;
- enter phase 4;
- active phase 2;
- path-A/path-B persistence;
- turn-parameter update;
- fast-stop / sharp-corner handling;
- publish;
- phase 1 history/timer maintenance;
- controller-specific hysteresis/interruption behavior.

The current function name/comments are therefore misleading.

### History

Commit `360662c...` attempted to execute REL 56405 directly on a fabricated
facade.

That approach crashed because the native controller/rebuild path dereferences
real rider/horse subsystems.

Commit `6f312f3...` removed that native execution and replaced it with the
safe sampler-only/manual-publish flow.

The comments in `FollowController.cpp` were not brought back in sync.

### Consequence

The final runtime behavior lacks most of the algorithm that makes horse road
following robust.

---

## Finding 2 — acquisition distance is wrong

Vanilla:

```text
controller->GetRoadDistance(latched)
        ↓
RoadSampleWrapper(... distance ...)
```

The public interface explicitly maps:

- OnPress: `RoadMagnetismOnPressRoadDistOff/On` (+0xD4/+0xD8)
- Auto: `RoadMagnetismAutoRoadDistOff/On` (+0xDC/+0xE0)

Current `TickNativeRoadFollow` passes:

```cpp
fn(rf, nullptr, 0.0f, &sample);
```

instead of the controller-derived distance.

### Consequence

Sampling/acquisition near the edge of a road is not using vanilla's
engage/remain radius. This is a direct candidate for:

- no useful hit when near the road edge;
- losing the road after a small lateral error;
- follow releasing/stalling after Henry drifts away.

The fix is not an arbitrary larger constant. Port the exact controller
`GetRoadDistance` behavior from Ghidra.

---

## Finding 3 — the steering command is being used as the wrong mathematical quantity

The RE notes correctly established:

- `m_magnetYaw` is a **yaw/steering command**, not world-space heading;
- `m_yawSmoothed` is a state smoothing that command;
- native `HorseYaw_SmoothCD` sends the current smoothed command downstream.

Current code instead:

1. seeds the smoother from `C_ActorPhysicsState::m_lookAngles.z`
   (absolute player view yaw);
2. smooths that absolute view yaw toward `m_magnetYaw`;
3. computes:

```cpp
delta = smoothedThisFrame - smoothedPreviousFrame;
```

4. uses only that **derivative** to turn Henry.

This mixes two incompatible quantities:

```text
absolute camera/view yaw
vs
native horse steering command
```

and then converts the result into a derivative that vanilla never uses as the
road-follow command.

### Consequence

This predicts the observed behavior very closely:

- an initial turn/arc occurs while the smoother is changing;
- as the smoother converges, `delta` tends toward zero;
- corrective steering disappears even though the native command may still
  require sustained turn input;
- Henry continues off-path or becomes stuck in an incorrect arc.

### Required work

Finish the downstream RE from:

```text
m_magnetYaw
 -> HorseYaw_SmoothCD
 -> {0, m_yawSmoothed}
 -> C_RiderSync / rider-sync consumer
 -> horse body/turn actuator
```

The exact consumer semantics must be recovered before mapping the command onto
Henry.

Do not treat the command as absolute world yaw or as per-frame derivative
without decompiler evidence.

---

## Finding 4 — synthetic W still makes movement camera-relative

The current autonomous movement still uses:

`HoldForward()`

which posts synthetic W.

The recovered `S_MountAnimState::m_desiredVelocity (+0x0C)` is already a
**world-rotated** movement vector produced by the ordinary on-foot movement
controller.

Current movement hook:

```cpp
originalMovementRequest(...);
desiredVelocity = Rotate(desiredVelocity, smallDelta);
```

Therefore the base desired velocity is still the normal W direction derived
from the current first-person control/view frame.

Moving the mouse changes that base movement vector.

### Consequence

Even with perfect path steering, this architecture cannot produce mounted-style
free headlook.

The player view and autonomous travel direction are not genuinely independent.

### Required architecture

During autonomous follow:

1. allow the normal controller to produce the frame request so all ordinary
   player state remains intact;
2. overwrite/replace the horizontal desired velocity with an **absolute
   world-space autonomous travel vector**;
3. that vector must be derived from a persistent Henry travel/body heading,
   not current camera yaw;
4. leave `m_deltaAngles` / mouse-look request untouched.

During manual WASD:

- stop overriding desired velocity;
- let the original controller's camera-relative vector through unchanged.

This gives a real ownership switch:

```text
WASD neutral  -> AutoWalk owns horizontal desiredVelocity in world space
WASD active   -> vanilla owns desiredVelocity
```

Synthetic W may still be useful to keep gait/forward-locomotion state alive,
but it must not define the autonomous world travel direction.

---

## Finding 5 — camera compensation is not the mounted camera mechanism

Current code attempts to preserve view direction with:

```cpp
state->m_lookAngles.z -= delta;
```

and sometimes falls back to:

```cpp
state->m_lookAngleAccum.z += delta;
```

This is not equivalent to mounted behavior.

Recovered native mounted behavior includes:

- horse body/yaw evolving independently;
- horse yaw delta forwarded to rider `m_lookAngleAccum`;
- rider camera compose;
- mounted centering state machine;
- wide/narrow/bottom view-limit setup/clamp;
- mouse request remaining a separate input channel.

The current foot code:

- directly mutates integrated look yaw;
- has no mounted-equivalent body reference frame;
- has no mounted view-limit installation;
- does not reproduce rider camera compose/centering semantics.

### Consequence

The result cannot feel like "free headlook while the body follows the road."

### Required work

First create real body/travel independence through the movement seam.

Then port the exact mounted view-reference/limit behavior.

Do not use camera mutation to compensate for a body-control architecture that
is still camera-relative.

---

## Finding 6 — native path persistence/chooser state is missing

Current runtime flow does not maintain the complete native:

- AutoController history/path vector;
- `S_HorseRoadFollow::m_pathA`;
- `m_pathB` backtrack history;
- snap/flick state;
- fork candidate continuity;
- trend/width weighting;
- enter/remain controller gates.

It adds an ad-hoc target gate:

```cpp
abs(rawTarget - previousTarget) <= EnterAngle
```

This is not equivalent to native branch/path continuity.

### Consequence

The sampler can switch candidate direction or lose continuity near:

- curves;
- forks;
- intersections;
- off-center approach.

The custom gate can then freeze an obsolete command rather than choosing the
same path the horse would.

---

## Finding 7 — manual WASD behavior is still custom

Current code hardcodes:

```text
manual input held >= 3 seconds -> disable follow
```

This is not the recovered OnPress controller behavior.

Ghidra recovered native:

- yaw/move threshold check;
- `RemainAngle`;
- `DeactivateTime`;
- `ReactivateTime`;
- armed state;
- return-to-neutral cancellation of interruption;
- eventual native deactivation.

### Required work

Remove the arbitrary 3-second rule and port the recovered OnPress state
machine exactly.

---

# Corrective implementation order

## Gate A — finish the missing downstream steering decompilation

Before changing steering code, recover exactly what consumes:

`{0, m_yawSmoothed}`

from `HorseYaw_SmoothCD`.

Determine whether it is:

- a turn input;
- angular rate;
- normalized steering command;
- relative desired angle;
- another quantity.

Trace until the physical horse hull/body yaw changes.

This is the critical missing semantic.

## Gate B — complete the safe C++ port of the road-follow state machine

Do not attempt another fake native object graph first.

Port the decompiled controller logic faithfully around the proven native road
sampler:

- exact `GetRoadDistance`;
- OnPress latch/acceptance gates;
- controller phases;
- path/history persistence;
- fork/snap/backtrack state;
- publish semantics;
- native interruption timers.

Use native CVars at the recovered offsets.

## Gate C — add a persistent autonomous travel frame

Maintain an AutoWalk travel/body heading independent from camera view.

Apply the recovered smoothed native steering command to that travel frame using
the semantics recovered in Gate A.

## Gate D — overwrite world-space desiredVelocity

At the verified movement-request seam:

- call vanilla first;
- if AutoWalk active and WASD neutral, preserve speed/gait magnitude but
  replace horizontal direction with the autonomous world-space travel frame;
- if WASD active, leave vanilla request untouched.

Do not rotate a camera-relative W vector by a derivative.

## Gate E — reproduce mounted headlook

Once body/travel is genuinely independent:

- leave mouse look input untouched;
- establish the same rider-style view reference to the moving body;
- apply the recovered mounted wide/narrow/bottom limits and centering rules;
- do not directly fight `m_lookAngles` every frame.

---

# Immediate code policy

Until Gates A-C are resolved:

- freeze `g_steerInvert`;
- freeze custom target-angle acceptance;
- freeze direct `m_lookAngles` counter-rotation;
- freeze movement-delta rotation;
- do not add another CTE/pure-pursuit/PID correction;
- do not increase miss grace/sample distance by guesswork.

These would mask the actual semantic bugs rather than fix them.


---

# Post-audit regression — commits a2f30dd / 975b112

The workstation read the audit and then implemented a new travel-frame controller
before completing **Gate A** (the downstream decompilation of the native steering
command). This reintroduced guesswork in a different form.

## Confirmed issue A — CryEngine forward-vector sign is wrong

Current movement hook writes:

```cpp
out[3] =  sin(g_travelYaw) * speed;
out[4] =  cos(g_travelYaw) * speed;
```

CryEngine defines local forward as `Vec3(0,1,0)`.

The engine math/public libKCD2 code gives the yaw convention directly:

```cpp
Quat::GetRotZ() = atan2(-GetFwdX(), GetFwdY());
```

and BetterHorseHandling uses the same convention for horse yaw:

```cpp
horseYaw = atan2f(-tm->m01, tm->m11);
```

Therefore for a horizontal yaw `z`, the forward XY vector is:

```text
x = -sin(z)
y =  cos(z)
```

not `(+sin(z), +cos(z))`.

The current write mirrors travel across the world Y axis. At headings around
+/-90 degrees this sends Henry directly opposite the body's true forward axis,
which is consistent with the reported "runs backwards" behavior.

The current seed:

```cpp
atan2(tm[4], tm[5])
```

equals the correct planar yaw only for an ideal orthonormal yaw-only matrix
because `m10 == -m01`. The authoritative engine expression is:

```cpp
atan2(-tm[1], tm[5])
```

(or derive the heading from Matrix34::GetColumn1).

On slopes/with non-planar rotation, use the engine's forward column/reference,
not the accidental m10 identity.

## Confirmed issue B — a2f30dd again treats m_magnetYaw as absolute world heading

The audit explicitly states that RE established:

`m_magnetYaw` is a **yaw/steering command, not world heading**.

Nevertheless a2f30dd now does:

```cpp
const float rawTarget = FootRoad::NativeMagnetYaw();
...
const float cmd = WrapPi(g_targetYaw - g_travelYaw);
SmoothCD(..., cmd, ...);
g_travelYaw += g_smoother.smoothed * sign;
```

That subtracts an absolute travel heading from a quantity already known **not**
to be an absolute world heading.

The commit comment also incorrectly renames m_magnetYaw as:

> "the road direction at the nearest point"

That contradicts the Ghidra findings already recorded in
`docs/REVERSE_ENGINEERING.md`.

This must be removed/reworked only after the exact downstream consumer of
`{0,m_yawSmoothed}` is decompiled.

## Unsupported issue C — integration units are guessed

The new code performs:

```cpp
g_travelYaw += g_smoother.smoothed;
```

with no `dt`.

Whether this is correct cannot be known until the downstream actuator is
recovered. `m_yawSmoothed` could be a normalized steering input, angular
rate, relative angular request, per-update delta, or another command.

Do not infer the units from observed behavior.

## Unsupported issue D — default sign inversion remains guessed

Current code keeps:

```cpp
g_steerInvert = 1;
sign = g_steerInvert ? -1.0f : 1.0f;
```

The sign must come from the recovered actuator path, not an inversion switch
left over from prototype testing.

## Prompt clarification

The on-foot prompt appearing is **not evidence that vanilla mounted prompt
logic somehow activates on foot**.

This mod deliberately patches:

- `Libs/Config/defaultProfile.xml` player action map with
  `foot_magnetism_activate/deactivate`;
- `Libs/Config/defaultActionHelp.xml` player help set with the Hold-E rows.

Therefore the on-foot prompt is expected whenever the mod marks those rows
visible. The disabled-action toast was an enabled/disable-reason state problem
inside the mod's custom player rows.

Do not spend RE time explaining why the prompt can render on foot; the repo
itself installs it there.

## Required next local action

Do **not** patch the vector sign and resume tuning as the primary plan.

The vector sign is a confirmed bug and should eventually be corrected, but the
larger steering path remains semantically invalid.

Connect GhidraMCP and finish Gate A:

```text
HorseYaw_SmoothCD
  -> {0, m_yawSmoothed}
  -> I_HorseRiderSync::PushRiderAction (slot 8 / 0x18059BC40)
  -> C_RiderSync implementation/data writes
  -> horse actor/movement request
  -> actual horse flat/body yaw update
```

Recover:

- exact meaning/units of each vector component;
- sign;
- whether dt is applied downstream;
- any clamps/scales;
- whether the command is a rate/input/delta;
- how it combines with manual rider turn;
- the exact final body/hull yaw actuator.

Only after this is known should `g_travelYaw` integration be implemented.


---

# 2026-10-07 live-test result after a5bec72

Observed on the game PC after the claimed "Gate A" completion:

- Henry still runs backwards;
- Henry still does not follow the road correctly;
- camera/view is still coupled to direction of travel.

This invalidates the claim that the actuator/camera architecture now matches vanilla.

## Gate A is NOT actually complete

The a5bec72 RE proves a transport/storage fact:

```text
PushRiderAction
 -> request+0x88 accumulator
 -> downstream request applier
```

It does **not** yet prove the final physical actuator semantics.

The documentation jumps from:

> the value is accumulated into request+0x88

to:

> therefore it is a per-update body-yaw delta with no further dt/scaling.

That conclusion requires decompiling the downstream request applier and tracing
the field until the horse hull/body yaw actually changes.

Until that final consumer chain is recovered, the following remain OPEN:

- exact units of request+0x88;
- whether downstream dt/scaling/clamping occurs;
- whether the value is normalized steering, angular request, turn fraction,
  or direct angular delta;
- sign convention at the body actuator;
- interaction with manual rider turn.

Gate A should be redefined as:

```text
m_yawSmoothed
 -> PushRiderAction
 -> request+0x88
 -> request applier
 -> movement/rotation subsystem
 -> actual horse flat/body yaw
```

and is complete only after the last step is understood.

## Confirmed orientation bug: wrong Matrix34 element

Commit bd21e8d attempted to fix CryEngine yaw seeding with:

```cpp
bodyYaw = atan2(-tm[4], tm[5]);
```

That is still wrong.

CryEngine Matrix34 is laid out as:

```text
tm[0]  = m00
tm[1]  = m01
tm[2]  = m02
tm[3]  = m03
tm[4]  = m10
tm[5]  = m11
tm[6]  = m12
tm[7]  = m13
tm[8]  = m20
tm[9]  = m21
...
```

and `Matrix34::GetColumn1()` -- the engine forward vector -- is:

```text
(m01, m11, m21)
= (tm[1], tm[5], tm[9])
```

The engine yaw convention is:

```cpp
atan2(-forward.x, forward.y)
```

Therefore the authoritative planar body-yaw seed is:

```cpp
atan2(-tm[1], tm[5])
```

not `atan2(-tm[4], tm[5])`.

Using m10 instead of m01 mirrors/sign-flips the orientation relationship and
can make the autonomous velocity point behind the actor.

Do not apply another empirical +/- sign workaround around this.

## Why "still runs backwards" is more than the matrix-index bug

Even after fixing the body-yaw seed, the current architecture only overrides:

`S_MountAnimState::m_desiredVelocity`

It does not establish an independent actor/body facing direction.

Henry's on-foot look state still drives/rebuilds:

- `m_lookAngles`;
- `m_viewRotation`;
- `m_flatYawQuat`;
- `m_flatYaw`.

Thus AutoWalk can request a world-space velocity that lies behind or sideways
relative to Henry's current body/view frame.

The locomotion/animation system can correctly interpret that as backward or
strafe movement.

So "velocity points along road" != "Henry's body faces/travels along road."

The body-facing seam must be recovered separately.

## Why the camera is still coupled

The current travel-frame patch removed direct camera counter-rotation, but it
never implemented the mounted body/view split.

Nothing in the current implementation reproduces the native mounted mechanisms
that keep body/hull yaw separate from rider view:

- actor flat-yaw/body orientation ownership;
- horse-yaw -> rider `m_lookAngleAccum`;
- rider camera compose;
- mounted view-limit channels;
- mounted recenter/reference-frame behavior.

Therefore the camera remaining tied to travel is expected.

A world-space desiredVelocity override alone cannot create horseback headlook.

## Exact next Ghidra targets

### 1. Finish the real horse actuator chain

Starting from the already recovered request+0x88 write, decompile through:

```text
C_RiderSync::PushRiderAction 0x18059BC40
 -> C_RiderPlayerControl / request applier (documented slot 69 path)
 -> downstream object vf[8]
 -> field/command consumer
 -> actual C_Horse movement/body-yaw mutation
```

Stop only when the code that physically updates horse body/hull orientation is
identified.

### 2. Recover Henry's on-foot body-facing seam

Decompile:

- `C_ActorMovementController::vf13` — REL 28376 / `0x1804B8E88`;
- `C_ActorPhysicsState::Tick` — REL 26156 / `0x1804415E0`;
- `C_Actor::SetViewRotation` — `0x1806440AC`;
- flat-yaw setter — `0x1806442A8`;
- owner hold-counter functions:
  - vf135 `0x18040B580`
  - vf136 `0x18286139C`.

Key question:

> How does vanilla permit view yaw to change without rebuilding the actor's
> body/flat yaw, and where can AutoWalk own that body yaw safely?

The upstream RE already notes that SetViewRotation **skips flat-yaw rebuild**
while either owner hold counter is positive. That is a high-priority lead, not
yet permission to call it blindly.

### 3. Recover the actual mounted camera split

Decompile together:

- `C_CameraFirstPerson::Compose` — REL 51045 / `0x18094D030`;
- `C_CameraRider::Compose` — REL 434788 / `0x1839C3748`;
- horse flat-yaw -> rider accumulator — REL 37998 / `0x1806CCAF8`;
- mounted centering — REL 56442 / `0x180A501E8`;
- mounted camera selection predicate `0x1809E6050`;
- view-limit clamp `0x18053B508`.

Do not implement free-look until the reference frame and flat-yaw ownership are
understood.

## Next implementation gate

No more travel-frame code changes until both are known:

1. the final physical semantics of the horse steering command;
2. the on-foot body-yaw ownership seam independent of view yaw.

The current live test demonstrates that neither has been solved yet.


---

# Review of bb27564 — body/view seam recovery

The new Ghidra work is materially useful, but one conclusion remains stronger
than the evidence currently documented.

## Proven

The following is now supported:

- SetViewRotation updates view yaw/pitch.
- With either actor hold counter positive, SetViewRotation does not rebuild the
  actor physics-state flat-yaw fields.
- SetFlatYaw can update m_flatYawQuat/m_flatYaw while +0x174 prevents it from
  rewriting the persistent view angles.
- Therefore +0x174 + SetFlatYaw provides a real mechanism for changing the
  actor physics-state flat yaw while leaving mouse-controlled view yaw alone.

That is the first credible native seam for camera/body separation.

## Not yet proven

The docs currently call m_flatYaw "the body's facing reference" and state that:

> the movement system faces the body along the flat yaw

but the recorded evidence does not yet show the on-foot movement/animation
controller consuming m_flatYaw/m_flatYawQuat to rotate Henry's physical body.

C_ActorPhysicsState is itself documented upstream as the look/view state
machine. A separate xref/dataflow proof is required before treating its flat
yaw as the final body actuator.

Required static check:

- decompile C_ActorMovementController::vf13 (REL 28376 / 0x1804B8E88);
- trace every read of the owner's flat-yaw state / GetViewRotation /
  entity forward frame involved in constructing m_desiredVelocity and root/body
  rotation;
- trace consumers of S_MountAnimState::m_rootRotation and the actor's physical
  orientation update;
- identify the function that actually rotates Henry's entity/animated body.

Only then decide whether SetFlatYaw is sufficient by itself or whether it is a
reference frame used by another body-facing stage.

## Documentation contradiction

docs/REVERSE_ENGINEERING.md currently contains both:

1. the new recommendation: hold +0x174 and drive SetFlatYaw(travelYaw), and
2. the older "Foot-adapter seam" recommendation to add smoothed road yaw to
   m_lookAngleAccum.

These are different architectures. The latter is obsolete for the autonomous
foot implementation and should not guide implementation work.

m_lookAngleAccum is a VIEW additive channel; using it as the road steering
actuator risks recoupling the camera to travel.

## Path-follow status is still independent

Even if the +0x174/SetFlatYaw seam proves correct, it fixes only body/view
ownership. It does not fix the poor road following.

The current runtime still does not execute/port the complete native
S_HorseRoadFollow + S_AutoController state machine. Sampler-only yawFrom plus
custom target/smoothing remains insufficient for curves, edges, forks, history,
snap/backtrack, and native hysteresis.

Therefore the next work should remain split into two explicit tracks:

A. Body/view actuator proof:
   prove how flat yaw reaches Henry's physical body, then implement the scoped
   hold safely.

B. Road-controller fidelity:
   port the recovered native controller phases/path state around the native
   sampler rather than continuing with yawFrom-only steering.

Do not treat success in track A as evidence that track B is solved.
