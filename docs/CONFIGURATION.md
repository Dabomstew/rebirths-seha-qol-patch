# Patch configuration

Edit `rebirths-patches.ini` beside the installed `X3DAudio1_7.dll`, then restart
the game. For a Re;Birth3 folder ending in `Re;Birth3`, these files are in its
`Birth3` subfolder. Keep your existing sections and other settings.

## Loose files

All four games can load loose files in place of files inside their PAC archives.
This is off by default. To enable it, add these settings to `[Patches]`:

```ini
[Patches]
LooseFiles=1
LooseFileDirectory=loosefiles
```

Place files under that folder using their archive namespace and internal path.
For example, a SYSTEM archive replacement goes at
`loosefiles/data/SYSTEM/database/stitem.gbin` inside the game installation.
The folder starts beside the game EXE, including Re;Birth3. Set
`LooseFileDirectory` to another relative folder or an absolute path to change it.
No asset preparation or manifest is needed for loose files.

To extract the originals for modding, choose **Extract loose files...** in the
preparer and confirm the destination. It extracts files from the selected game's
archives and installed DLC, then enables `LooseFiles`. Existing files, including
your mods, are kept. Extraction uses the original files without story-image
downscaling. **Cancel** stops safely and keeps completed files; run the action
again to continue. A failed or cancelled extraction does not enable the option.
The game must be closed during extraction.

Some Re;Birth2 copies contain one Help image that cannot be extracted safely.
The preparer reports that file and keeps its original archive available.

For each PAC file request, the game tries the loose file first, then prepared
assets if `[Patches] UncompressedAssets=1`, then the original archive. Loose files
also work with `UncompressedAssets=0`. Missing or unreadable loose files fall
through to the next source; unavailable prepared assets fall through to the
original archive. Empty files count as present replacements. Files larger than
2,147,483,647 bytes and paths containing `..` are skipped. Linked mod files and
folders are supported. Loose files are trusted as supplied: there are no content,
hash, original-size, format or manifest checks, even with
`[UncompressedAssets] Verify=1`. The patch does not change a replacement to make
it compatible with the game.

Set `LooseFiles=0` to disable overrides. Missing or empty toggle values default
to `0`; nonnumeric text reads as `0`, and other nonzero integers enable it.
A missing `LooseFileDirectory` defaults to `loosefiles`; an empty or overlong
directory disables the loose provider while preserving the other sources.
Updates preserve both settings. Restart the game after changing them.
`rebirths-patches.log` records the enabled asset providers at startup.

## Enemy-turn freeze fix

All four games support this setting in the `[Patches]` section:

```ini
[Patches]
EnemyTurnSoftlockFix=1
```

It prevents an enemy-turn freeze that can occur when battle planning finishes
before the game starts waiting for it. It preserves the planned actions and
normal battle progression. It is enabled by default and has no Prepare Game
checkbox. Set it to `0` to disable it. Updates preserve your explicit choice.
The same setting also retries failed planning-worker creation and safely cancels
pending work or waits for active work when leaving battle.

Use exactly `0` or `1`. The existing Windows integer reader treats a missing or
empty value as the default (`1`), nonnumeric text as `0`, and other integers as
enabled when nonzero. The fix activates only for a supported executable whose
expected code matches. Check `rebirths-patches.log` beside the DLL for
`EnemyTurnSoftlockFix installed` or `EnemyTurnSoftlockFix disabled`.

## Integrity diagnostics

These checks are off by default. Use them to diagnose changed game files; they
can slow startup and loading. Restart the game after changing either setting.

```ini
[UncompressedAssets]
Verify=0

[AudioTail]
VerifyBanks=0
```

Set `Verify=1` to check prepared manifests, original archive indexes and every
opened prepared asset. Set `VerifyBanks=1` to check complete voice and effect
banks before trimming their endings, and compare repeated voice metadata reads.
Voice banks can exceed 2 GB, so this check
can substantially delay startup. These options take effect only when the
corresponding feature is active. Trimming stays enabled with `VerifyBanks=0`.

Use `0` or `1`. Missing or empty values default to `0`, nonnumeric text reads as
`0`, and other nonzero integers enable the checks. Updates keep saved choices.
Normal play retains format, size, ownership and expected audio-word checks.
The patch checks the target executable once at startup and shares that result
across its features.

## Save Anywhere

Set `SaveAnywhere=1` in `[Patches]` to add Save to the System menu inside a
dungeon. It uses the normal save-slot selection and confirmation dialogs. Load
keeps its normal availability. This setting is off by default and has no
preparer checkbox. Set it to `0` to disable it; updates preserve your choice.

Use `0` or `1`. Missing or empty values default to `0`, nonnumeric text reads as
`0`, and other nonzero integers enable it. Restart after editing the INI. The
option activates only for a supported executable with matching menu code.

Dungeon saving and usable reloads have been checked in Re;Birth1, Re;Birth2,
Re;Birth3 and Neptune VS Sega Hard Girls.
