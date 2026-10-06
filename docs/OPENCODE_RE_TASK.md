# OpenCode Task — Native Horse Road-Follow Reconstruction

Use this document as the local agent's task definition.

## Objective

Reverse engineer the exact KCD2 mounted road-follow controller from the copied `WHGame.dll`. Do not infer the algorithm from gameplay recordings and do not tune the existing AutoWalk prototype.

## Read first

1. `AGENTS.md`
2. `docs/DECOMPILATION_PLAN.md`
3. `docs/REVERSE_ENGINEERING.md`
4. `docs/UPSTREAM_RE_FINDINGS.md`
5. `re/seed_manifest.json`

## Target

`.re/input/WHGame.dll`

Verify `.re/target.json` matches the expected supported build before applying absolute addresses.

## First assignment

1. Open `VTABLE_S_AutoController` from the seed manifest.
2. Resolve all four vtable entries.
3. Apply the known `I_MagnetismController` slot contract.
4. Fully decompile and type `S_AutoController::Tick`.
5. Recursively decompile every nontrivial helper it calls.
6. Apply `S_HorseCVars` names to CVar reads by offset.
7. Trace every persistent read/write to `S_AutoController`, `S_HorseRoadFollow`, `S_HorseData`, and path buffers.
8. Then decompile `S_HorseRoadFollow::Tick` and connect its input/output to the concrete AutoController functions.
9. Write normalized pseudocode and a state-transition/dataflow summary into `docs/REVERSE_ENGINEERING.md`.
10. Commit and push textual findings. Do not commit IDA databases or `WHGame.dll`.

## Stop condition

Do not implement Henry control changes until the native AutoController and HorseRoadFollow state machine are branch-complete enough that no steering behavior requires guessing.
