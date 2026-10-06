# Research Sources

## Official KCD2 modding

- Installing mods: https://github.com/muyuanjin/kcd2-mod-docs/blob/main/official-wiki/KM-A-56%20Installing%20mods/README.md
- Mod structure: https://github.com/muyuanjin/kcd2-mod-docs/blob/main/official-wiki/KM-A-1%20Modding%20Kingdom%20Come%20Deliverance%202/KM-A-36%20Technical%20Overview/KM-A-3%20Structure%20of%20a%20Mod/README.md
- Manifest: https://github.com/muyuanjin/kcd2-mod-docs/blob/main/official-wiki/KM-A-1%20Modding%20Kingdom%20Come%20Deliverance%202/KM-A-36%20Technical%20Overview/KM-A-3%20Structure%20of%20a%20Mod/KM-A-57%20Mod%20Manifest/README.md
- Publishing: https://github.com/muyuanjin/kcd2-mod-docs/blob/main/official-wiki/KM-A-1%20Modding%20Kingdom%20Come%20Deliverance%202/KM-A-36%20Technical%20Overview/KM-A-58%20Publishing%20a%20mod/README.md

Official docs establish manual `Mods/<mod>` loading, required `mod.manifest`, lowercase/underscore-only `modid`, optional version support matching, and Steam Workshop support.

## KCSE

https://www.nexusmods.com/kingdomcomedeliverance2/mods/3332

KCSE installs `dinput8.dll` beside `KingdomCome.exe` under `Bin/Win64MasterMaster*PGO`. Current KCSE uses an external Address Library.

This repository does not redistribute KCSE.

## Address Library

External runtime dependency:

```text
<game>/KCSE/addresslib/
```

Do not bundle or re-upload it.

## libKCD2

https://github.com/JerryYOJ/libKCD2

Pinned revision:
`10d20f28faba462c4bf98a01abb48225cc51bb91`

It provides the KCSE API, REL infrastructure, CryEngine headers, and current public reverse engineering used here.

## BetterHorseHandling

https://github.com/JerryYOJ/libKCD2/tree/10d20f28faba462c4bf98a01abb48225cc51bb91/Projects/BetterHorseHandling

Useful precedent includes direct rider-input access and checking live road magnetism.

## Horse Route Follow

https://www.nexusmods.com/kingdomcomedeliverance2/mods/3742

Conceptual value: it filters/chooses road candidates offered by vanilla auto-follow rather than directly steering the horse.

The MVP here does not need its map-marker routing/Dijkstra layer.
