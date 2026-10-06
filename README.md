# KCD2 AutoWalk

Native KCSE mod for **Kingdom Come: Deliverance II** intended to reuse the game's horse road-magnetism / road-follow logic while Henry is on foot.

## Target behavior

- KCD2's native horse road-follow logic determines the road and desired travel direction.
- Henry receives a full-strength on-foot movement vector along that direction.
- Vanilla locomotion remains responsible for speed state:
  - Caps Lock: walk
  - normal: jog
  - Shift: sprint
- Camera remains free.
- Horse acceleration, collision avoidance, animation, and physics are not transplanted.

This is not a W-key macro and not a custom road graph.

## Current status

**Instrumentation milestone.** The repo currently scaffolds the native KCSE plugin and diagnostics. It does not yet mutate Henry's movement.

The two remaining native seams before movement is enabled are:

1. verify the direct road-sampler call contract currently associated with `sub_180A0A124`;
2. verify the on-foot `C_PlayerInput` / `C_PlayerMovementAction` movement-vector injection point.

## Compatibility

Pinned libKCD2:

`10d20f28faba462c4bf98a01abb48225cc51bb91`

That revision states KCD2 Steam **1.5.6** as its native target. KCSE and a matching KCSE Address Library are required.

## Prerequisites

- Windows 10/11
- Visual Studio 2022, Desktop development with C++
- Git
- CMake
- vcpkg
- `VCPKG_ROOT` set to the vcpkg checkout
- KCD2 + KCSE + matching Address Library for runtime testing

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

Or one development cycle:

```powershell
.\scripts\dev.ps1 -Configuration Debug
```

Installed layout:

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

The scripts never install or overwrite KCSE, the Address Library, game executables, or another mod.

## Diagnostic commands

- `kcse_autowalk_status`
- `kcse_autowalk_probe_horse`

The second command is a mounted-horse read-only probe for already mapped road-magnetism fields.

## Roadmap

1. build/load verification;
2. mounted road-magnetism observations;
3. direct on-foot road sampling;
4. on-foot movement-vector injection;
5. toggle/cancel behavior;
6. compatibility guards and release packaging;
7. Steam Workshop testing only after KCSE Workshop plugin discovery is verified.

See `docs/` and `AGENTS.md`.

## License

GPL-3.0.
