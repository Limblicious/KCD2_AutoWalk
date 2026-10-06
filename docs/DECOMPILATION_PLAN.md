# Decompilation Plan — Native Horse Road Follow

## Purpose

Recover the exact KCD2 mounted path-follow implementation from the supported `WHGame.dll` and use that as the design specification for AutoWalk.

The purpose is not to observe the horse and infer the algorithm.

The purpose is to read the algorithm.

## Tooling

Preferred workstation setup:

- exact supported KCD2 `WHGame.dll` copied to a dedicated RE working directory;
- IDA + Hex-Rays decompiler;
- IDA MCP server connected to OpenCode or another local agent;
- libKCD2 source open alongside IDA for existing types/REL IDs/RTTI;
- IDB and binary working copies gitignored and never committed.

## Seed information already known

- `S_HorseRoadFollow::Tick` — REL 56405;
- road wrapper — REL 194146;
- state builder — REL 194136;
- road sampler — REL 55123;
- `S_AutoController` type/factory/vtable;
- `S_OnPressController` type/factory/vtable;
- `I_MagnetismController` interface;
- `S_HorseData`, `S_HorseRoadFollow`, `S_HorseFollowStick`, `S_HorseMagnetismSample`;
- `C_RiderPlayerInput`;
- `S_HorseCVars`;
- `C_CameraRider::Compose` — REL 434788;
- `C_CameraFirstPerson::Compose` — REL 51045;
- horse-yaw -> rider view glue — REL 37998;
- camera centering — REL 56442.

Verify all identifiers against the exact local build.

## Phase A — S_HorseRoadFollow::Tick

Recover:

1. exact signature and all callers;
2. every early-exit condition;
3. controller mode creation/selection;
4. latch handling;
5. rider-input reads;
6. road-query/sample construction;
7. path-vector reads/writes;
8. `I_MagnetismController::Tick` call semantics;
9. all `S_HorseData` writes;
10. exact output/return semantics.

Deliverable: branch-complete typed pseudocode.

## Phase B — S_AutoController

Resolve and decompile every virtual function:

- destructor;
- `SetHoldLatched`;
- `Tick`;
- `GetRoadDistance`.

Recursively decompile every nontrivial helper.

Identify exactly:

- acquire/remain/deactivate states;
- interruption state/timing;
- direction continuity;
- road scoring;
- path-width scoring;
- trend scoring;
- crossroad prediction;
- backtracking;
- snap behavior;
- flick handling;
- persistent path history;
- use of `m_pseudoSpeed`;
- use of rider input/stick;
- desired-yaw generation.

Name every CVar read from `S_HorseCVars`.

Do not summarize a helper as "probably chooses road." Decompile it.

## Phase C — S_OnPressController / hold E

Trace backwards from `SetHoldLatched`, `m_latched`, and controller creation.

Recover:

- physical/action input corresponding to E;
- press/hold/release transitions;
- timing source;
- road-distance conditions;
- failure conditions;
- transition into active magnetism;
- what remains latched after release.

Deliverable: exact activation state machine.

## Phase D — Manual rider input

Trace all reads of:

- `C_RiderPlayerInput::m_move`;
- `m_turn`;
- `m_stickMag`;
- `S_HorseFollowStick::m_stick`;
- packed/state-machine input fields.

Determine exactly:

- whether short WASD blends with magnetism;
- which components override steering;
- interruption accumulation;
- deactivation thresholds;
- differences among forward/lateral/backward input;
- what happens when manual input returns to zero.

Deliverable: deterministic manual-input/falloff state machine.

## Phase E — Steering output chain

Trace from controller output until horse movement/orientation changes.

Recover:

1. desired road direction -> `m_magnetYaw`;
2. `m_yawSmoothed` / `m_yawVel`;
3. speed/gait turn limits;
4. slowdown coupling;
5. final rider/horse turn request.

This defines the exact output boundary AutoWalk should consume.

## Phase F — Mounted camera

Decompile:

- `C_CameraRider::Compose`;
- relevant `C_CameraFirstPerson::Compose`;
- REL 37998;
- REL 56442;
- setup/update code for rider view-limit channels.

Recover:

- yaw/pitch reference frame;
- exact values/CVars;
- wide vs narrow selection;
- pitch/bottom behavior;
- smoothing;
- recenter rules;
- magnetism-specific conditions;
- cleanup on dismount.

Deliverable: exact camera behavior spec.

## Phase G — Synthetic facade feasibility

After A-F, enumerate every dependency the native controller dereferences.

Build a dependency table covering transform, speed, rider input, road-follow state, path vectors, controller object, and any horse-only systems.

Determine whether the original controller can run using Henry transform/speed/input plus synthetic horse state.

If yes, use it directly.

If no, identify the minimum blocking dependency and translate only that part.

## Phase H — Runtime validation

Only after the code is understood.

Hook the same native tick for mounted and synthetic controllers and log at full native cadence.

Runtime data answers:

> Did we reproduce the decompiled code correctly?

It does not answer:

> What do we think the code probably does?

## Completion criteria

The static reconstruction is complete when the workstation can explain from decompiled code exactly:

- how follow engages;
- how road/path selection is maintained;
- how forks/intersections are handled;
- how manual WASD affects controller state;
- when follow deactivates;
- how steering is computed/smoothed;
- how the rider camera remains independent/constrained.
