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
