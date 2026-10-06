# Reverse-Engineering Workstation Handoff

This is the **first task** for the game-PC/OpenCode session after pulling `main`.

## 1. Pull and prepare a non-destructive target copy

```powershell
git pull --ff-only
.\scripts\prepare-re.ps1
```

This copies the installed `WHGame.dll` into:

```text
.re/input/WHGame.dll
```

and writes:

```text
.re/target.json
```

with SHA-256, file version, size, and detected Address Library filenames.

The installed game binary is never modified.

If the local game was updated since the expected 1.5.6 / 15693 target, stop and reconcile compatibility before applying absolute VAs from `re/seed_manifest.json`.

## 2. Open the copy in IDA/Hex-Rays

Open only:

`.re/input/WHGame.dll`

Create the IDB inside `.re/`.

Do not commit the IDB.

## 3. Connect IDA MCP to OpenCode

The workstation agent should have direct decompiler/xref/type/rename access.

Before doing any native implementation work, give the agent these repo files:

- `AGENTS.md`;
- `docs/DECOMPILATION_PLAN.md`;
- `docs/REVERSE_ENGINEERING.md`;
- `docs/UPSTREAM_RE_FINDINGS.md`;
- `re/seed_manifest.json`.

## 4. Apply seeds before exploring

Use `re/seed_manifest.json` to name known functions/vtables/RTTI only after validating the target build.

First resolve the **four entries at `VTABLE_S_AutoController`** and rename the slot targets according to the known interface contract:

1. destructor;
2. `SetHoldLatched`;
3. `Tick`;
4. `GetRoadDistance`.

Then do the same for the OnPress controller.

This immediately yields the concrete implementation functions instead of circling around dispatch wrappers.

## 5. First decompilation milestone

Do not implement anything yet.

Complete phases A and B of `docs/DECOMPILATION_PLAN.md`:

- full `S_HorseRoadFollow::Tick`;
- full `S_AutoController`;
- all nontrivial helpers called from those functions.

Apply `S_HorseCVars` offset names as each float/int global is recognized.

Commit only textual findings/type declarations/source changes—not IDA artifacts.

## 6. Second milestone

Then recover:

- OnPress / hold-E activation;
- rider WASD interaction;
- desired-yaw and smoothing chain.

Only after these are branch-complete should the workstation investigate the Henry adaptation seam.

## 7. Camera milestone

Decompile the mounted camera separately rather than mixing it into steering work:

- Rider compose;
- FirstPerson compose comparison;
- horse-yaw view accumulator;
- centering;
- view-limit installation/clamp.

## 8. Reporting format

For every recovered function add to `docs/REVERSE_ENGINEERING.md`:

```text
Function:
VA:
REL ID:
Recovered signature:
Callers:
Callees:
Persistent reads:
Persistent writes:
CVars:
Branches/state transitions:
Output:
Confidence/evidence:
```

For complex functions include normalized pseudocode, not raw Hex-Rays output.

## 9. No prototype tuning

Do not spend workstation time adjusting:

- `g_steerGain`;
- CTE constants;
- sample frequency;
- synthetic mouse turn scaling;
- hard WASD cancellation.

That prototype is frozen while native reconstruction is underway.
