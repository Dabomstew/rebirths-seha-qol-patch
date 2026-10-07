# Rebirths / Sega Hard Girls QoL Patch

Version **0.2.2**.

A Windows patch and preparer for **Hyperdimension Neptunia
Re;Birth1, Re;Birth2, Re;Birth3**, and **Superdimension Neptune VS Sega Hard Girls**.

Prepare game assets ahead of time for faster loading and speed up texture
conversion. The patch also offers tutorial skipping, shorter quiet or silent
audio endings, and optional story-scene speedups. Available features vary by game.

## Installation and configuration

1. Download and verify the release ZIP, then extract it outside the game folder.
2. Close the game and run **Rebirths-Preparer.exe**.
3. Select your game folder, review the settings, and choose **Prepare / Resume**.
4. Click **Play**, or launch through Steam after preparation completes.

**Install / Update** installs the patch and saves settings without preparing
assets. **Play** does the same before starting the game. Assets without prepared
copies use the original archives; use **Prepare / Resume** to build them.

You need 64-bit Windows. The preparer includes the patch DLL and its runtime;
no Python or compiler is needed to install it. It accepts supported original
game EXEs and supported versions with the 4GB patch. See the
[User Guide](docs/USER-GUIDE.md) for supported versions and features, disk space,
updates, rollback and removal.

New installations enable prepared assets, fast texture conversion, tutorial
skipping, and smaller versions of selected large story illustrations. Audio tail
trimming starts on in the three Re;Birth games; Sega's movement fix starts on.
Story fast-forward and story auto-skip start off. Updates keep your saved settings
and asset profile. Other optional features use the defaults listed in the guide.

If the preparer finds a patch DLL it doesn't recognize, it stops without replacing
it. For the usual Re;Birth3 folder, a Windows path-search bug requires the patch
DLL and settings to go under `Birth3/`. The preparer handles this automatically;
select the folder containing the game EXE.

## Compatibility and limits

Each supported game version and feature has been checked separately. Testing
covers selected routes; other scenes and combinations of mods may still have
problems. The patch can fall back to the original archives. Prepared assets are
created from your own installation and aren't included in downloads. See [Quick Start](docs/QUICK-START.md),
[User Guide](docs/USER-GUIDE.md), [Asset format](docs/ASSETS.md), and
[Troubleshooting and validation](docs/TESTING.md).

## Support and source builds

Report problems through [GitHub Issues](https://github.com/Dabomstew/rebirths-seha-qol-patch/issues), including
the patch version, which game you're playing, and a description of the problem
with private paths removed. Keep game
files, saves, extracted assets and private paths out of reports. Installation
doesn't mean the patch is allowed in your speedrun category; check its rules.

Developers: see [Build and verify](docs/BUILD.md). No game data is distributed.
The patch source is [MIT licensed](LICENSE); see [Third-party notices](THIRD-PARTY-NOTICES.md).

## AI use

AI tools helped with research, coding, and documentation under human direction.
The documented tests describe what has been checked. They don't cover every
installation or a complete playthrough of each game.
