# Ghidra Setup for KCD2 AutoWalk

## Exact supported RE stack

### Ghidra

Use **Ghidra 12.1.4**, the current official NSA release.

Release:
https://github.com/NationalSecurityAgency/ghidra/releases/tag/Ghidra_12.1.4_build

Asset:
`ghidra_12.1.4_PUBLIC_20260921.zip`

Published SHA-256:
`ddac49f903da9d5bac833e5cc79395098b9c33cfd3279be5f31bd00387d2d4db`

Ghidra 12.1.x requires a full **JDK 21**.

Suggested install:

```text
C:\Tools\ghidra_12.1.4_PUBLIC
```

Set:

```powershell
$env:GHIDRA_HOME = "C:\Tools\ghidra_12.1.4_PUBLIC"
```

### GhidraMCP

Use **themixednuts/GhidraMCP v0.9.0**.

Repository:
https://github.com/themixednuts/GhidraMCP

Release:
https://github.com/themixednuts/GhidraMCP/releases/tag/v0.9.0

Asset:
`GhidraMCP-0.9.0.zip`

Published SHA-256:
`6caa1c1af7643851d4ee2c88ee5137189159957307fbd26de32db69852c552ba`

v0.9.0 explicitly targets Ghidra 12.1.4.

Install:

1. Start Ghidra.
2. `File > Install Extensions...`
3. click `+`
4. select `GhidraMCP-0.9.0.zip`
5. check the extension
6. restart Ghidra
7. open CodeBrowser
8. `File > Configure > Configure All Plugins`
9. enable **GhidraMCP**
10. `Tools > GhidraMCP > Start MCP Server`

Default local endpoint:

`http://127.0.0.1:8080/mcp`

Keep it on localhost.

## OpenCode

Current OpenCode supports remote Streamable HTTP MCP servers.

From this repo:

```powershell
opencode mcp add ghidra --url http://127.0.0.1:8080/mcp
opencode mcp list
```

Equivalent current v2 config:

```jsonc
{
  "$schema": "https://opencode.ai/config.json",
  "mcp": {
    "servers": {
      "ghidra": {
        "type": "remote",
        "url": "http://127.0.0.1:8080/mcp",
        "oauth": false,
        "timeout": {
          "startup": 30000,
          "catalog": 30000,
          "execution": 600000
        }
      }
    }
  }
}
```

A copy is stored in `re/opencode-ghidra.example.jsonc`.

## Why this GhidraMCP

The project needs persistent local access to:

- decompilation and decompiled-code search;
- listings/disassembly;
- xrefs/references;
- function naming/prototype/variable updates;
- labels/symbols;
- struct/union/enum creation and updates;
- data-type application;
- RTTI/call-graph/vtable analysis;
- comments/bookmarks;
- project analysis/save.

GhidraMCP v0.9.0 exposes those operations directly inside Ghidra, so there is no cloud decompiler and no separate Python bridge process.

## Prepare the project

After installing the stack:

```powershell
.\scripts\prepare-re.ps1
.\scripts\check-re-tools.ps1
.\scripts\ghidra-import.ps1
```

Then open `.re/ghidra/KCD2_AutoWalk_RE.gpr` and follow `docs/RE_WORKSTATION.md`.
