# Rebirths / Sega Hard Girls QoL Patch

Version **0.2.0**.

A shared Windows patch and native preparer for **Hyperdimension Neptunia
Re;Birth1, Re;Birth2, Re;Birth3**, and **Superdimension Neptune VS Sega Hard Girls**.
Game-specific identities, offsets and supported features remain separate.

Prepared assets reduce repeated preparation work; texture conversion, silent
audio-tail trimming, tutorial controls and optional ADV speedups supplement each
game's native paths. Features retain independent game-specific guards.

## Installation and configuration

1. Download and verify the release ZIP, then extract it outside the game folder.
2. Close the game and run **Rebirths-Preparer.exe**.
3. Select a supported installation, review settings, and choose **Prepare / Resume**.
4. Launch through Steam after preparation completes.

The utility uses an x64 native preparer and an x86 `X3DAudio1_7.dll` proxy with
static C++ runtimes. No Python or compiler is required for normal installation.
It recognizes supported original and qualified LAA executable variants. Check
[User Guide](docs/USER-GUIDE.md) for executable identities, per-game support,
storage requirements, update, rollback and removal.

Fresh installations enable prepared assets, fast texture conversion, tutorial
skipping, and the cataloged large-ADV-CG downscale profile. Silent audio-tail
trimming defaults on in the three Re;Birth games; Sega's movement fix defaults on.
ADV fast-forward and auto-skip remain off. Existing explicit settings and asset
profiles are preserved. Other optional features retain their documented defaults.

Do not manually overwrite an unknown proxy. For the usual Re;Birth3 folder,
Windows' semicolon search-path behavior requires managed proxy/configuration
placement under `Birth3/`; the preparer handles this automatically.

## Compatibility and limits

Supported executables and features are qualified independently per game.
Results cover bounded routes, not every scene or mod combination. Original
archives remain available for native fallback; prepared data is local and is
never supplied in a release. See [Quick Start](docs/QUICK-START.md),
[User Guide](docs/USER-GUIDE.md), [Asset format](docs/ASSETS.md), and
[Troubleshooting and validation](docs/TESTING.md).

## Support and source builds

Report problems through [GitHub Issues](https://github.com/Dabomstew/rebirths-seha-qol-patch/issues), including
the patch version, game identity and a sanitized diagnostic summary. Keep game
files, saves, extracted assets and private paths out of reports. Installation
does not establish speedrun-category eligibility; check your category rules.

Developers: see [Build and verify](docs/BUILD.md). No game data is distributed.
The patch source is [MIT licensed](LICENSE); see [Third-party notices](THIRD-PARTY-NOTICES.md).

## AI use

AI coding tools assisted research, implementation, and documentation under human
direction. Compatibility and correctness are assessed through the documented
tests. Coverage remains limited; passing checks do not establish compatibility
with every installation or correctness throughout an entire game.
