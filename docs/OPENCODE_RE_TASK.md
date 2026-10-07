# OpenCode Task — Ghidra Native Horse Road-Follow Reconstruction

## Objective

Use connected GhidraMCP tools to reverse engineer the exact KCD2 mounted road-follow controller from the copied `WHGame.dll`.

Do not infer the algorithm from gameplay recordings and do not tune the existing AutoWalk prototype.

## Read first

1. `AGENTS.md`
2. `docs/DECOMPILATION_PLAN.md`
3. `docs/REVERSE_ENGINEERING.md`
4. `docs/UPSTREAM_RE_FINDINGS.md`
5. `re/seed_manifest.json`
6. `re/type_layouts.json`
7. `re/horse_cvars.json`
8. `re/autowalk_types.h`

## Preconditions

- `.re/target.json` matches the supported 1.5.6 / 15693 binary.
- Ghidra project `KCD2_AutoWalk_RE` is open.
- `WHGame.dll` is the active CodeBrowser program.
- GhidraMCP is connected in OpenCode.
- Auto-analysis and `ApplyAutoWalkSeeds.java` completed.

## First assignment

1. Analyze `VTABLE_S_AutoController` at `0x183EAAE18`.
2. Verify its four targets: destructor, `SetHoldLatched`, `Tick`, `GetRoadDistance`.
3. Apply meaningful names/prototypes in Ghidra.
4. Create/import known structures from `re/type_layouts.json` / `re/autowalk_types.h` and apply only where supported by evidence.
5. Fully decompile `S_AutoController::Tick`.
6. Recursively decompile every nontrivial helper. Do not stop at descriptions like "probably chooses a road."
7. Name CVar reads using `re/horse_cvars.json`.
8. Trace persistent reads/writes to `S_AutoController`, `S_HorseRoadFollow`, `S_HorseData`, road samples, and path buffers.
9. Decompile `S_HorseRoadFollow::Tick` and connect controller/sample/input/output flow.
10. Use xrefs around `0x180A09944` and `0x180A09D7E` to recover vanilla fork/snap candidate enumeration/filtering.
11. Record normalized pseudocode and state/dataflow findings in `docs/REVERSE_ENGINEERING.md`.
12. Save the Ghidra project.
13. Commit/push textual/source findings only. Never commit `.re/`, the Ghidra project, or `WHGame.dll`.

## GhidraMCP rules

- Prefer decompilation + xrefs + types over raw assembly guessing.
- Persist useful function prototypes, symbols, data types, comments, and bookmarks in Ghidra.
- After changing a type/prototype, re-decompile affected callers/callees.
- For uncertain semantics, use comments/bookmarks rather than confident misleading names.
- Resolve indirect calls to concrete vtable/function-pointer targets before summarizing behavior.
- Keep MCP local to `127.0.0.1`.

## Stop condition

Do not implement Henry-control changes until AutoController + HorseRoadFollow are branch-complete enough that no steering behavior requires guessing.
