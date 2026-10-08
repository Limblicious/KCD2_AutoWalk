# Steam Workshop — Release 0.1.0

## Uploader

`H:\SteamLibrary\steamapps\common\KCD2Mod\Tools\SteamWorkshopUploader\SteamWorkshopUploader.exe`

Official docs: Warhorse modding wiki — "Publishing a mod" (KM-A-58). The uploader takes the folder **containing `mod.manifest`** (not a ZIP), a cover image <= 1 MB, a description, tags, and a visibility setting. Authorization requires the owner's Steam account (launch the uploader normally; it uses the logged-in Steam client).

## Upload folder

`dist\kcd_autowalk\`

```text
kcd_autowalk/
├── mod.manifest          (0.1.0, supports 1.5.6*)
├── mod.cfg               (kcse_autowalk_debug 0)
├── data/AutoWalkData.pak
├── Localization/English_xml.pak
└── KCSE/Plugins/KCD2_AutoWalk.dll
```

Backup ZIP: `dist\KCD2_AutoWalk-0.1.0.zip`

## Workshop item fields

- **Title**: `AutoWalk — Follow Roads on Foot`
- **Mod folder**: `H:\VSCodeRepos\KCD2_AutoWalk\dist\kcd_autowalk`
- **Cover image**: in-game screenshot (see below), <= 1 MB
- **Visibility**: Hidden for the Workshop-only install test, Public only after confirmation
- **Tags**: Gameplay, Utilities, User Interface

## Workshop description

```
Follow roads on foot — just like riding.

Stand on a road and hold E to latch onto the path. Henry walks the road
automatically, using the game's own horse path-follow logic, while you
enjoy the scenery.

- Hold E near a road to start following
- W/A/S/D take over instantly — let go to resume
- Menus pause and resume following automatically
- Works only on foot; riding, sprinting and combat are untouched

REQUIREMENTS
- KCD2 1.5.6
- KCSE (Kingdom Come Script Extender)
- KCSE Address Library for 1.5.6

INSTALLATION
KCSE loads native plugins from the game's Mods folder. Install KCSE
(dinput8.dll into Bin/Win64MasterMasterSteamPGO) and the Address Library
(KCSE/addresslib), then make sure this mod's folder is present in
KingdomComeDeliverance2/Mods/kcd_autowalk. After subscribing, copy the
downloaded Workshop folder there — KCSE does not read Workshop content
directly yet.

KNOWN LIMITATIONS
- The camera stays tied to Henry's travel heading while following
- The "not on suitable road" warning is English-only for now
- Quick-save while following is untested
```

## KCSE loader finding (verified against source)

KCSE `PluginManager::Init()` scans only:
1. `<game root>\mods\*\KCSE\Plugins\`
2. `<game root>\KCSE\Plugins\`

Steam Workshop subscriptions are mounted by the game's own mod system from the Steam content folder, which KCSE does not read. Therefore a Workshop-only install will deliver the data/localization paks but **not** load the native plugin until the mod folder is also present under `Mods\`. The hidden-upload test below confirms this empirically via `KCSE\logs\KCSE.log`.

## Workshop-only install test (hidden upload)

1. Back up the working manual install: move `Mods\kcd_autowalk` to `Mods_backup\kcd_autowalk` (do not delete).
2. Subscribe to the hidden item; let Steam download it.
3. Launch the game; read `KCSE\logs\KCSE.log` — "Searching mods directory" shows whether the plugin was discovered.
4. If not discovered (expected): copy the downloaded Workshop folder into `Mods\kcd_autowalk` and relaunch.
5. In-game checklist:
   - hold-E prompt appears on a road
   - Henry follows the road
   - W interrupts; releasing resumes
   - I/P/M/J/alchemy screens do not scroll on their own
   - following resumes after closing menus
   - "Henry is not on suitable road" warning appears off-road
6. Restore/remove the manual backup afterwards so only one copy exists.

## Cover image (not yet captured)

Capture in-game: Henry standing on a road with the hold-E prompt visible, daytime, 16:9, PNG or JPG under 1 MB. No third-party artwork.
