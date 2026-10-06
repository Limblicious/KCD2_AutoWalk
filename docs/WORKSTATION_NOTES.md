# Workstation Notes

## Environment

- KCD2 version: 1.5.6 (Steam), build `release_1_5-15693`
- KCSE version: installed (external, not redistributed)
- KCSE gameVersionRaw: 1.5.6 (per KCSE.log)
- KCSE release index: n/a (recorded in KCSE.log)
- Address Library file: `KCSE/addresslib/kcd_addresslib_steam_release_1_5-15693.bin`
- Visual Studio: 2022 Community 17.14 (via `KCD2_VS_INSTANCE` workaround)
- CMake: VS-bundled, on PATH
- vcpkg: VS-bundled via `VCPKG_ROOT`
- libKCD2 commit: `10d20f28faba462c4bf98a01abb48225cc51bb91`

## Build

Passing (Debug). `scripts/build.ps1` passes `-DCMAKE_GENERATOR_INSTANCE`.
`vcpkg.json` carries baseline `2e776305f2542cc626716435b0c1baf67bb5ab45`.

## Plugin load

Verified: KCSE registers `KCD2_AutoWalk.dll`; PreDataLoaded / DataLoaded /
LoadGame fire cleanly. Logs: `Mods/kcd_autowalk/KCD2_AutoWalk.log`,
`KCSE/logs/KCSE.log`.

## Mounted magnetism probes

`kcse_autowalk_probe_horse` implemented (read-only telemetry of
S_HorseData road-follow state). Live gameplay values pending user run.

## Direct sampler investigation

RESOLVED (static analysis, 1.5.6 address library):

- wrapper REL 194146 / state builder REL 194136 / sampler REL 55123;
  standard x64; see docs/REVERSE_ENGINEERING.md Open item A.
- Clone-facade probe `kcse_autowalk_probe_footroad` implemented
  (mounted precondition, read-only). Zeroed standalone facade deferred.

## On-foot movement investigation

RESOLVED (static analysis):

- `SimulateOnAction` -> player vtable slot 148 (REL 48720) with interned
  action name from the six manager slots at `whGlobal()+0x30`; mode
  `eAAM_OnPress = 1`. See docs/REVERSE_ENGINEERING.md Open item B.
- Console diagnostic `kcse_autowalk_simulate <name> <mode> <value>`
  implemented (single-shot, user-invoked). Live press/release semantics
  for `moveforward` / `xi_rotateyaw` pending user run.

## Next runtime steps (user-driven)

1. In-game, mounted on a road: run `kcse_autowalk_probe_footroad` and
   `kcse_autowalk_probe_horse`; hit/yaw values should agree.
2. In-game, on foot: run `kcse_autowalk_simulate moveforward 1 1` (expect
   held-W movement), then `kcse_autowalk_simulate moveforward 0 0` (release).
3. Report results; only then implement the recurring tick + steering.
