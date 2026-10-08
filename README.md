# KCD2 AutoWalk

Follow roads on foot in **Kingdom Come: Deliverance II**. Stand on a road, hold **E** like you would on horseback, and Henry latches onto the path and walks it by himself.

AutoWalk is a native [KCSE](https://www.nexusmods.com/kingdomcomedeliverance2/mods/3332) plugin. It does not fake the road following with custom steering: it translates the game's own mounted road-magnetism controller onto Henry's on-foot locomotion, so the road choice, continuity, forks and backtracking behave like the vanilla horse feature.

## What it does

- **Hold E** near a road to engage path following — the same prompt and latching feel as on horseback.
- While following and **not touching WASD**, Henry walks the road automatically using the native road-follow logic.
- Pressing **W/A/S/D** immediately hands control back to normal movement; the follow state survives like it does on horseback. Release the keys and Henry resumes if the road latch is still active.
- Opening any menu (ESC, inventory, map, perks, alchemy, ...) suspends the auto-input so menus never scroll on their own, and following resumes when you close them.
- Henry shows a proper hint when he is not on a suitable road.
- Normal walking, sprinting, stamina, combat and everything else are untouched. The mod only acts on foot.

## Requirements

- Kingdom Come: Deliverance II (Steam) **1.5.6**
- [Kingdom Come Script Extender (KCSE)](https://www.nexusmods.com/kingdomcomedeliverance2/mods/3332)
- A KCSE Address Library for 1.5.6 (`kcd_addresslib_steam_release_1_5-15693.bin`)

KCSE and the Address Library are separate downloads and are not bundled with this mod.

## Installation

KCSE loads native plugins from the game's `Mods` folder.

1. Install **KCSE**: put its `dinput8.dll` into `...\KingdomComeDeliverance2\Bin\Win64MasterMasterSteamPGO\`.
2. Install the **KCSE Address Library**: the game root must contain `KCSE\addresslib\kcd_addresslib_steam_release_1_5-15693.bin`.
3. Install **AutoWalk**: copy the `kcd_autowalk` folder into `...\KingdomComeDeliverance2\Mods\` so the game root looks like:

```text
KingdomComeDeliverance2/
├─ Bin/Win64MasterMasterSteamPGO/dinput8.dll   (KCSE)
├─ KCSE/addresslib/kcd_addresslib_*.bin        (Address Library)
└─ Mods/
   └─ kcd_autowalk/
      ├─ mod.manifest
      ├─ mod.cfg
      ├─ data/AutoWalkData.pak
      ├─ Localization/English_xml.pak
      └─ KCSE/Plugins/KCD2_AutoWalk.dll
```

### Steam Workshop note

Subscribing on the Steam Workshop delivers the mod's data and localization packs, but KCSE does not discover native plugins inside Steam Workshop content. Copy `KCSE\Plugins\KCD2_AutoWalk.dll` from the downloaded Workshop item into the game's global `KCSE\Plugins` folder. This loads only the native plugin manually and avoids installing a duplicate copy of the Workshop data PAKs. If KCSE gains native Workshop plugin discovery, this step becomes unnecessary.

### Unlimited Saving II compatibility

[Unlimited Saving II](https://steamcommunity.com/sharedfiles/filedetails/?id=3443741661) and AutoWalk both replace `Libs/Config/defaultProfile.xml`, so installing both without a compatibility patch causes whichever loads first to lose its input actions. Build the optional merged patch with:

```powershell
.\scripts\package-usii-compat.ps1 -Configuration Release -Version 0.1.0
```

Then copy `dist\kcd_autowalk_usii_compat` into the game's `Mods` folder alongside `kcd_autowalk`. Keep Unlimited Saving II subscribed. The compatibility mod is data-only and preserves the normal AutoWalk package unchanged.

## How to use

1. Stand on or near a road.
2. **Hold E** until the prompt engages (same interaction as mounting path follow).
3. Hands off — Henry follows the road.
4. **W/A/S/D** take over immediately; let go to resume.
5. Menus pause and resume following automatically.

## Known limitations

- While following, the camera stays tied to Henry's travel heading. The mounted-style decoupled look (free camera like on horseback) is not implemented yet.
- Henry's warning text ("Henry is not on suitable road") is localized in English; other languages show the vanilla text.
- The hold-E prompt requires shipping full replacements of two vanilla input config files. A future game update that changes those files may require a mod update.
- Unlimited Saving II requires the optional compatibility package described above because both mods replace the same input profile.

## Compatibility

- KCD2 Steam **1.5.6** (build `release_1_5-15693`)
- KCSE with a matching Address Library
- libKCD2 pinned: `10d20f28faba462c4bf98a01abb48225cc51bb91`

## Building from source

```powershell
git clone https://github.com/Limblicious/KCD2_AutoWalk.git
cd KCD2_AutoWalk
.\scripts\bootstrap.ps1
.\scripts\build.ps1 -Configuration Release
.\scripts\package.ps1 -Configuration Release -Version 0.1.0
```

`.\scripts\install.ps1 -Configuration Release` packages and installs into the local game `Mods` folder.

## Development and reverse engineering

The road-follow implementation was recovered from `WHGame.dll` (Ghidra 12.1.4 + GhidraMCP) and mirrors the native mounted pipeline:

- `S_HorseRoadFollow::Tick` / `S_AutoController` / `S_OnPressController` semantics
- hold-to-engage latching (`HintsActive`, OnPress slot-1)
- path sampling, magnetism, and yaw smoothing (`PublishMagnetism`, `HorseYaw_SmoothCD`)
- rider input interruption and resume behavior
- menu/full-UI mode hooks for input suspension

The pure state machine lives in `src/RoadFollowMachine.*` with deterministic tests in `tests/RoadFollowMachineTests.cpp`.

See `docs/ARCHITECTURE.md`, `docs/REVERSE_ENGINEERING.md`, `docs/TEST_PLAN.md`, and `AGENTS.md` for the full record.

## License

GPL-3.0
