# Workstation Procedure

First local task:

```powershell
git pull --ff-only
.\scripts\bootstrap.ps1
.\scripts\build.ps1 -Configuration Debug
.\scripts\package.ps1 -Configuration Debug
.\scripts\diagnose.ps1
```

If KCSE and Address Library are present:

```powershell
.\scripts\install.ps1 -Configuration Debug
.\scripts\diagnose.ps1
```

Launch KCD2 and collect:

1. `kcse_autowalk_status`;
2. mounted probe with magnetism inactive;
3. mounted probe with road follow on a straight road;
4. curve;
5. fork;
6. relevant KCSE errors.

Write factual results to `docs/WORKSTATION_NOTES.md`.

If build fails, capture the first error and tool versions. Do not copy random headers/binaries into the game or dependency tree.

If game startup fails, remove only this mod:

```powershell
.\scripts\uninstall.ps1 -Confirm
```

Do not remove KCSE, Address Library, WHGame.dll, KingdomCome.exe, or other mods as an attempted fix.
