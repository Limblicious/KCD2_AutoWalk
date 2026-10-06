# Agent Policy — KCD2_AutoWalk

Treat the KCD2 installation as production data.

## Objective

Implement on-foot road following that behaves like KCD2's mounted path-follow feature by recovering and reusing/translating the **actual native horse controller logic** from `WHGame.dll`.

The final implementation must not be based on hand-tuned approximations when the corresponding native logic can be recovered from the binary.

## Decompiler-first rule

For any behavior already implemented by KCD2, **do not guess it from runtime observations** and do not invent replacement math before the native implementation has been fully decompiled.

This specifically applies to:

- road acquisition;
- path candidate selection;
- fork/crossroad logic;
- backtracking;
- path-width weighting;
- snap/trend/flick behavior;
- enter/remain/deactivate hysteresis;
- manual rider input interruption/falloff;
- steering/yaw generation and smoothing;
- hold-E activation/latching;
- mounted camera decoupling;
- mounted look limits and camera centering.

Runtime captures are secondary validation only. They are not the source used to infer the algorithm.

If the decompiler can expose the code path, recover it first.

## Required reverse-engineering workflow

Use a local copy of the exact supported `WHGame.dll` in IDA/Hex-Rays, preferably through an MCP-connected workstation agent.

For each target function:

1. locate it by known REL ID / RVA / xrefs;
2. apply existing libKCD2 structure types and RTTI;
3. recover the complete signature;
4. decompile every branch and nontrivial helper it depends on;
5. name persistent state and intermediate values;
6. trace all CVar reads;
7. identify every persistent field written across frames;
8. identify every downstream output;
9. document the recovered pseudocode/dataflow in `docs/REVERSE_ENGINEERING.md`;
10. only then implement the dismounted adapter.

Do not replace missing understanding with guessed constants or control laws.

## User-facing control contract

### Engagement

- On foot near/on a valid road, **hold E** to request path follow.
- Engagement/latching should use the native mounted semantics recovered from code.

### Autonomous follow

While follow is active and WASD is neutral:

- Henry moves forward through normal on-foot locomotion.
- Henry's body/travel heading follows the recovered native road controller.
- Camera yaw is decoupled from travel direction.
- Mouse look remains user-controlled.
- Camera view limits match mounted rider behavior.
- The mod must not steer Henry by injecting mouse motion.

### Manual movement

When W/A/S/D is non-neutral:

- normal on-foot manual movement becomes authoritative immediately;
- do not destroy follow state just because a key was pressed;
- native rider interruption/deactivation logic determines whether the follow survives.

When WASD becomes neutral:

- resume autonomous follow if the native controller remains latched/active;
- otherwise remain manual until E is held again.

## Never

- delete, move, rename, patch, or replace `KingdomCome.exe`, `WHGame.dll`, or base-game PAKs;
- install, update, overwrite, or remove KCSE automatically;
- install, update, overwrite, or remove the KCSE Address Library automatically;
- modify another mod;
- recursively delete outside this repository or the exact validated `Mods/kcd_autowalk` uninstall target;
- run `robocopy /MIR`, `git clean -fdx`, destructive reset, force checkout, or force push;
- commit game binaries, KCSE binaries, Address Library files, extracted proprietary assets, IDBs, decompiler databases, dumps, or machine-specific absolute paths.

## Dependency policy

libKCD2 is pinned to:

`10d20f28faba462c4bf98a01abb48225cc51bb91`

Do not float against upstream during normal builds.

## Native-hook policy

- Prefer KCSE Address Library / `REL::ID`.
- No unexplained hard-coded absolute addresses.
- Seed the decompiler with existing libKCD2 RTTI, vtables, REL IDs, types, and known names.
- Every mutation hook fails closed.
- Never "try an address and see if it crashes."
- Work on a copied/local binary for decompilation; never alter the installed `WHGame.dll`.

## Architecture constraints

### Recover from KCD2

Fully recover before replacement:

- `S_HorseRoadFollow::Tick`;
- `S_AutoController::Tick`;
- `S_OnPressController`;
- `I_MagnetismController::SetHoldLatched`;
- `I_MagnetismController::GetRoadDistance`;
- sampler/state-builder helpers;
- rider input -> magnetism interaction;
- magnetism -> desired yaw -> smoothing;
- mounted camera compose and view-limit setup.

### Keep from Henry

Do not transplant horse gait, acceleration, collision avoidance, jump, slope physics, animation/bridle state, or body dimensions.

Henry retains on-foot locomotion, gait, stamina, collision, and animation.

## Explicitly deprecated prototype behavior

Do not tune the current prototype into the final system:

- 15 Hz sample loop;
- `along-hit` tangent steering;
- custom cross-track error;
- custom exponential heading smoothing;
- synthetic mouse-delta steering;
- hard WASD cancel.

Those proved that movement injection is possible. They are not the target controller.

## Runtime testing role

Runtime tests are used **after** decompilation to verify that the recovered implementation produces the expected state/output.

Do not attempt to reconstruct frame timing or hidden controller state from video or low-frequency telemetry when the code can be read directly.

## Current milestone

Complete the static reconstruction in `docs/DECOMPILATION_PLAN.md` before making further architecture decisions about the final follower.
