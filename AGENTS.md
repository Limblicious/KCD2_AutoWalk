# Agent Policy — KCD2_AutoWalk

Treat the KCD2 installation as production data.

## Objective

Implement on-foot road following that **behaves like KCD2's mounted path-follow feature**, including its native road-selection/steering state machine and rider-style camera independence.

The mod should adapt the mounted system to Henry on foot rather than replace it with a custom path controller.

## User-facing control contract

### Engagement

- On foot near/on a valid road, **hold E** to request path follow.
- Engagement timing/latching should mirror the native horseback interaction as closely as practical.

### Autonomous follow state

While path follow is engaged and the player is not touching WASD:

- Henry's locomotion continues forward automatically.
- Henry's movement/facing follows the native road-follow controller.
- Camera yaw is decoupled from Henry's travel direction.
- Mouse look remains user-controlled.
- View yaw/pitch limits must match the mounted rider camera behavior; do not permit unrestricted 360-degree look.

### Manual movement

When any WASD input is present:

- manual on-foot movement becomes authoritative immediately;
- controls are normal camera-relative on-foot controls;
- do not cancel road follow merely because a key was pressed;
- native magnetism/interruption logic should decide whether follow remains latched or falls off after enough manual deviation/input.

When WASD returns to neutral:

- if native magnetism is still active, autonomous road-follow resumes;
- otherwise remain manual until E is held again.

## Never

- delete, move, rename, patch, or replace `KingdomCome.exe`, `WHGame.dll`, or base-game PAKs;
- install, update, overwrite, or remove KCSE automatically;
- install, update, overwrite, or remove the KCSE Address Library automatically;
- modify another mod;
- recursively delete outside this repository or the exact validated `Mods/kcd_autowalk` uninstall target;
- run `robocopy /MIR`, `git clean -fdx`, destructive reset, force checkout, or force push;
- commit game binaries, KCSE binaries, Address Library files, extracted proprietary assets, IDBs, decompiler databases, dumps, or machine-specific absolute paths.

Generated cleanup is limited to known repo paths such as `build/`, `dist/`, and `.deps/`.

## Dependency policy

libKCD2 is pinned to:

`10d20f28faba462c4bf98a01abb48225cc51bb91`

Do not float against upstream during normal builds. Changing the pin requires explicit compatibility review and documentation updates.

## Native-hook / reverse-engineering policy

- Prefer KCSE Address Library / `REL::ID`.
- No unexplained hard-coded absolute addresses.
- Seed the decompiler with existing libKCD2 RTTI, vtables, REL IDs, types, and known names.
- Document every recovered native call in `docs/REVERSE_ENGINEERING.md`.
- Every mutation hook fails closed.
- Never "try an address and see if it crashes."
- Work on a copied/local binary for decompilation; never alter the installed `WHGame.dll`.

## Architecture constraints

### Reuse from KCD2

Recover and reuse/translate, in priority order:

- `S_HorseRoadFollow::Tick`;
- `S_AutoController::Tick`;
- hold/latch activation semantics;
- acquire/remain/deactivate hysteresis;
- path vectors and backtracking;
- crossroad prediction;
- road-width/trend/flick/snap logic;
- native desired yaw / smoothing semantics;
- rider WASD intervention behavior;
- mounted camera decoupling and view limits.

### Keep from Henry

Do not transplant horse:

- gait/acceleration;
- collision avoider;
- jump behavior;
- slope physics;
- horse animation/bridle state;
- horse body dimensions.

Henry retains on-foot locomotion, gait, stamina, collision, and animation.

## Explicitly deprecated prototype behavior

Do **not** continue tuning the current custom:

- 15 Hz sample loop;
- `along-hit` tangent steering;
- custom cross-track error;
- custom exponential heading smoothing;
- synthetic mouse deltas used to whip the camera toward the road;
- hard cancel on any WASD press.

Those were useful to prove feasibility but do not satisfy the target UX.

## Workstation cycle

```powershell
git pull --ff-only
.\scripts\bootstrap.ps1
.\scripts\build.ps1 -Configuration Debug
.\scripts\package.ps1 -Configuration Debug
.\scripts\install.ps1 -Configuration Debug
.\scripts\diagnose.ps1
```

Local runtime/decompiler findings belong in `docs/WORKSTATION_NOTES.md` and `docs/REVERSE_ENGINEERING.md`.

## Current milestone

Before replacing the prototype follower, map the complete mounted control pipeline:

1. native auto-controller road-follow state machine;
2. how manual rider input blends/interferes with magnetism;
3. activation/latching via the mounted follow-path action;
4. mounted camera selection/compose, view limits, and horse-yaw-to-rider-camera glue;
5. the clean dismounted seams needed to supply native steering without forcing camera yaw.
