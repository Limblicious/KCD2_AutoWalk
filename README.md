# KCD2 AutoWalk

Native KCSE mod for **Kingdom Come: Deliverance II** intended to make Henry follow roads on foot by reusing the game's **actual mounted road-magnetism / road-follow controller**, not by approximating it with custom steering.

## Target behavior

The intended experience should mirror vanilla horseback path follow:

1. Henry is on foot and near/on a valid road.
2. The player **holds E** to engage path follow, matching the vanilla horse interaction.
3. Once engaged and the player is **not touching WASD**:
   - KCD2's native horse road-follow controller determines the road, continuation, and steering;
   - Henry continues moving forward using normal on-foot locomotion;
   - Henry's travel/facing direction is **decoupled from camera yaw**;
   - mouse look is free so the player can enjoy the scenery;
   - look rotation is constrained by the **same mounted camera/view limits** used while riding (no unrestricted 360-degree spin).
4. When the player supplies **W/A/S/D**:
   - manual movement immediately becomes authoritative;
   - movement uses normal on-foot camera-relative controls;
   - mouse look behaves normally for manual locomotion;
   - the native magnetism controller remains responsible for whether road follow survives the intervention or naturally deactivates after enough deviation/input, just as it does on horseback.
5. When manual movement stops:
   - if vanilla magnetism is still active/latched, autonomous road following resumes;
   - if vanilla logic has disengaged it, Henry remains under normal manual control until the player holds E again.

Caps Lock, Shift, stamina, collision, slopes, animation, and normal on-foot movement speed remain vanilla.

## Important architecture rule

This project must **not** reproduce horse path following with a homemade tangent/CTE/PID-style controller if the native controller can be executed or translated directly.

The current experimental implementation on `main` proves that Henry can be moved and that native roads can be sampled, but its 15 Hz road sampling + custom cross-track correction + synthetic mouse steering is **not the target implementation**. That code is diagnostic/prototype work only.

The next implementation phase is to reverse the complete mounted pipeline:

- `S_HorseRoadFollow::Tick`
- `S_AutoController::Tick`
- `S_OnPressController` / hold-to-engage behavior
- native path vectors, hysteresis, crossroad prediction, backtracking, snap/trend logic
- how rider WASD influences magnetism and disengagement
- native `m_magnetYaw` / smoothing output
- rider camera decoupling, mounted camera limits, and camera recentering

Then the mod should reuse that behavior with a dismounted actuator.

## Current status

The workstation has already demonstrated:

- clean KCSE build/install;
- native road sampling from Henry's position;
- synthetic held-W movement;
- synthetic input events;
- a first experimental on-foot road follower.

That experimental follower is intentionally considered **superseded architecture** because it turns the camera to steer Henry and bypasses most of KCD2's native road-follow state machine.

## Compatibility

Pinned libKCD2:

`10d20f28faba462c4bf98a01abb48225cc51bb91`

Current runtime target:

- KCD2 Steam 1.5.6
- build `release_1_5-15693`
- KCSE
- matching KCSE Address Library

## Build

```powershell
git clone https://github.com/Limblicious/KCD2_AutoWalk.git
cd KCD2_AutoWalk
.\scripts\bootstrap.ps1
.\scripts\build.ps1 -Configuration Debug
```

Package/install:

```powershell
.\scripts\package.ps1 -Configuration Debug
.\scripts\install.ps1 -Configuration Debug
```

Or:

```powershell
.\scripts\dev.ps1 -Configuration Debug
```

## Installed layout

```text
KingdomComeDeliverance2/
└─ Mods/
   └─ kcd_autowalk/
      ├─ mod.manifest
      ├─ mod.cfg
      ├─ KCD2_AutoWalk.log
      └─ KCSE/
         └─ Plugins/
            └─ KCD2_AutoWalk.dll
```

## Next game-PC task

Do **not** continue tuning the current prototype follower.

On the game PC:

```powershell
git pull --ff-only
.\scripts\prepare-re.ps1
```

Then follow:

- `docs/RE_WORKSTATION.md`
- `docs/OPENCODE_RE_TASK.md`
- `docs/DECOMPILATION_PLAN.md`

The repo already contains:

- `re/seed_manifest.json` — known functions, REL IDs, vtables, RTTI, and vanilla road-chooser anchors;
- `re/type_layouts.json` — mapped controller/view-state layouts;
- `re/horse_cvars.json` — named road-magnetism/steering/camera CVar offsets;
- `re/ghidra_scripts/ApplyAutoWalkSeeds.java` — applies known function/vtable/RTTI names and resolves controller vtables;
- `re/ghidra_scripts/DumpAutoWalkVtables.java` — prints concrete controller vtable slot targets;
- `re/autowalk_types.h` — Ghidra-importable known controller/view-state layouts.

The goal of the first local session is to decompile the actual `S_AutoController` and `S_HorseRoadFollow` implementation, not to modify Henry's movement.

## Reverse-engineering direction

The preferred local workflow is **Ghidra 12.1.4 + GhidraMCP v0.9.0 + OpenCode**, using a copied `WHGame.dll` and a Ghidra project under ignored `.re/`.

GhidraMCP runs locally inside Ghidra and exposes decompilation, xrefs, symbols, prototypes, data types/structs, RTTI, and vtable analysis to OpenCode. Existing libKCD2 symbols/REL IDs are applied as seeds instead of starting from an unnamed binary.

See `docs/GHIDRA_SETUP.md` for the exact tool versions and connection procedure.

See `docs/ARCHITECTURE.md`, `docs/REVERSE_ENGINEERING.md`, `docs/TEST_PLAN.md`, and `AGENTS.md`.

## License

GPL-3.0.
