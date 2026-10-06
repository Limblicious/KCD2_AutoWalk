# Test Plan

## 0. Reproducible build

```powershell
.\scripts\bootstrap.ps1
.\scripts\build.ps1 -Configuration Debug
.\scripts\package.ps1 -Configuration Debug
```

Pass:

- pinned libKCD2 verified;
- dependencies resolve;
- `build/bin/Debug/KCD2_AutoWalk.dll` exists;
- packaged DLL exists under `dist/kcd_autowalk/KCSE/Plugins`.

## 1. Installation safety

Run diagnose before/after install. Only `<game>/Mods/kcd_autowalk` may change.

## 2. Plugin load

Expected log:

`<game>/Mods/kcd_autowalk/KCD2_AutoWalk.log`

Run:

`kcse_autowalk_status`

Record output in `docs/WORKSTATION_NOTES.md`.

## 3. Mounted magnetism probe

Run `kcse_autowalk_probe_horse`:

1. mounted, magnetism inactive;
2. straight-road follow active;
3. curve;
4. fork;
5. disengage.

Correlate `magnetismLive`, mode, yaw, and hit values with visible behavior.

## 4. Direct sampler — future

Before movement mutation, prove road acquisition from Henry's on-foot position on the same test roads.

## 5. Movement seam — future

Inject known world headings with camera at aligned, ±90°, and 180°. Verify free camera, native walk/jog/sprint, stamina, collision, and clean disable.

## 6. Integrated follow — future

Test straight road, curves, junctions, village roads, bridge/narrow path, obstacle, road loss, manual cancel, mount, dialogue, ladder, save/load.

## Release gate

No release until unsupported versions fail closed, install/uninstall is scoped, no stuck movement is possible, and dependency/license notices are complete.
