# Reverse-Engineering Workstation Handoff — Ghidra

This is the first task for the game-PC/OpenCode session after pulling `main`.

## Toolchain

Use the exact stack documented in `docs/GHIDRA_SETUP.md`:

- Ghidra 12.1.4
- JDK 21
- GhidraMCP v0.9.0 from `themixednuts/GhidraMCP`
- OpenCode connected to the local MCP endpoint

## 1. Pull and stage a non-destructive copy

```powershell
git pull --ff-only
.\scripts\prepare-re.ps1
```

This copies the installed `WHGame.dll` to `.re/input/WHGame.dll` and writes `.re/target.json` with SHA-256, version, size, and Address Library filenames.

The installed DLL is read/copied only.

If the game no longer matches KCD2 1.5.6 / release_1_5-15693, stop before applying absolute address seeds.

## 2. Verify tools

Set `GHIDRA_HOME` to the extracted Ghidra 12.1.4 directory, then run:

```powershell
.\scripts\check-re-tools.ps1
```

## 3. Create/analyze the Ghidra project

```powershell
.\scripts\ghidra-import.ps1
```

This analyzes the copied binary and stores the project under:

```text
.re/ghidra/KCD2_AutoWalk_RE.gpr
.re/ghidra/KCD2_AutoWalk_RE.rep/
```

It runs `re/ghidra_scripts/ApplyAutoWalkSeeds.java` after auto-analysis so known functions/vtables/RTTI and controller vtable targets are named before agent work begins.

Use `-Rebuild` only when intentionally rebuilding this ignored local project.

## 4. Open Ghidra

Open `.re/ghidra/KCD2_AutoWalk_RE.gpr` and then `WHGame.dll` in CodeBrowser.

Never patch the installed game's DLL.

## 5. Start GhidraMCP

Inside CodeBrowser:

1. `File > Configure > Configure All Plugins`
2. enable **GhidraMCP**
3. `Tools > GhidraMCP > Start MCP Server`
4. keep it on `127.0.0.1:8080`

Then:

```powershell
opencode mcp list
```

The `ghidra` server should show connected.

## 6. Give OpenCode the prepared task

Read in order:

1. `AGENTS.md`
2. `docs/OPENCODE_RE_TASK.md`
3. `docs/DECOMPILATION_PLAN.md`
4. `docs/REVERSE_ENGINEERING.md`
5. `docs/UPSTREAM_RE_FINDINGS.md`
6. `re/seed_manifest.json`
7. `re/type_layouts.json`
8. `re/horse_cvars.json`
9. `re/autowalk_types.h`

## 7. First milestone

Do not implement Henry movement yet.

Using GhidraMCP:

- inspect `VTABLE_S_AutoController`;
- verify its four concrete targets;
- apply the `I_MagnetismController` slot contract;
- create/apply known controller structures;
- fully decompile `S_AutoController::Tick`;
- recursively decompile every nontrivial helper;
- then fully decompile `S_HorseRoadFollow::Tick`;
- connect both dataflow/call graphs.

Persist useful renames, types, prototypes, comments, and bookmarks in the Ghidra project.

Commit only textual findings/source changes, never `.re/` or the copied binary.

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

For complex functions include normalized pseudocode, not raw decompiler output.

## 9. Prototype freeze

Do not tune:

- `g_steerGain`;
- CTE constants;
- sample frequency;
- synthetic mouse turn scaling;
- hard WASD cancellation.

The prototype remains frozen until native reconstruction is complete.
