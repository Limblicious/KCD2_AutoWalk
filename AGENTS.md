# Agent Policy — KCD2_AutoWalk

Treat the KCD2 installation as production data.

## Objective

Implement on-foot road following by reusing KCD2's native horse road-magnetism/path-follow logic for road acquisition and steering while preserving Henry's native locomotion, gait, stamina, collision, animation, and normal on-foot camera/facing behavior.

## Never

- delete, move, rename, patch, or replace `KingdomCome.exe`, `WHGame.dll`, or base-game PAKs;
- install, update, overwrite, or remove KCSE automatically;
- install, update, overwrite, or remove the KCSE Address Library automatically;
- modify another mod;
- recursively delete outside this repository or the exact validated `Mods/kcd_autowalk` uninstall target;
- run `robocopy /MIR`, `git clean -fdx`, destructive reset, force checkout, or force push;
- commit game binaries, KCSE binaries, Address Library files, extracted proprietary assets, dumps, or machine-specific absolute paths.

Generated cleanup is limited to known repo paths such as `build/`, `dist/`, and `.deps/`.

## Dependency policy

libKCD2 is pinned to:

`10d20f28faba462c4bf98a01abb48225cc51bb91`

Do not float against upstream during normal builds. Changing the pin requires explicit compatibility review and documentation updates.

## Native-hook policy

- Prefer Address Library / `REL::ID`.
- No unexplained hard-coded absolute addresses.
- If a signature scan is required, document the signature, target, semantics, validation, and failure path.
- Every mutation hook fails closed.
- Never "try an address and see if it crashes."
- Do not call mapped virtual methods on unsupported versions without compatibility evidence.

## Architecture constraints

Reuse native road acquisition, road segment information, road tangent/heading, and fork semantics where practical.

Do not transplant horse gait, acceleration, collision avoidance, jump, slope, animation/bridle, or horse physics.

Henry's speed remains vanilla. AutoWalk should sustain ordinary forward movement and apply road-follow steering through the least invasive on-foot heading/turn seam.

Do **not** invent horse-style camera independence for Henry. Do not rotate or decouple the camera as a separate system, and do not convert road direction into a camera-relative strafe vector just to preserve the camera's world-space orientation. Leave KCD2's existing on-foot camera/facing coupling alone.

## Workstation cycle

```powershell
git pull --ff-only
.\scripts\bootstrap.ps1
.\scripts\build.ps1 -Configuration Debug
.\scripts\package.ps1 -Configuration Debug
.\scripts\install.ps1 -Configuration Debug
.\scripts\diagnose.ps1
```

On failure, diagnose the failing step. Do not improvise changes to the game installation.

Local runtime findings belong in `docs/WORKSTATION_NOTES.md`.

## Current milestone

No Henry movement mutation until both are verified:

1. direct native road sampler call contract;
2. safe on-foot forward-input and heading/turn steering seam.

Mounted horse probing is allowed because it is read-only.
