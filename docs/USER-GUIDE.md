# Re;Birth release preparer

## Settings and actions

**Install / Update** installs the patch and saves the displayed settings without
preparing or scanning assets. **Prepare / Resume** also prepares and verifies
assets. **Play** installs the patch and saves your settings, then starts the selected
game if installation succeeds. Until prepared assets are available, the patch
loads from the original archives. Use **Prepare / Resume** to build them.

**Reload settings** reads saved choices again. If another program changes the
settings while this window is open, the preparer stops and asks you to reload
before saving. If you switch games, reload, close the window or start maintenance
with unsaved changes, you can **Discard changes** or **Stay** and keep editing.
Use **Game folder...** to choose an installation and
**Prepared assets...** to choose its destination. Destination paths can also be
selected, copied and edited using absolute paths. The detected-game dropdown
also lets you select and copy its full path; choose a game from its list or use
**Game folder...** to change the installation.

The window scales with the display and can be resized. Settings are grouped for
the selected game; unavailable game-specific settings are omitted. During
preparation, **Close** becomes **Cancel**, which stops at a safe boundary.
Closing the window during work waits for safe completion. Installation and
maintenance finish before the window closes. Closing also cancels any pending game launch.
**Rollback last update** and **Uninstall patch** retain prepared assets.

## Games and asset location

This app prepares game assets and installs the patch for Re;Birth1, Re;Birth2, Re;Birth3, or Neptune VS Sega Hard Girls. It requires 64-bit Windows. Select a detected Steam installation, or click **Game folder...** and choose the folder containing the game EXE. The preparer checks that your game version is supported before making changes. It leaves the original game archives intact.

For Re;Birth2, the preparer recognizes both known versions of `GAME00001.pac`, including the version with the repaired help texture. It refuses an unknown source version before installing anything.

The game and output folders can be reached through linked parent folders, such as a Steam library path on Linux. The selected folders themselves and files or subfolders inside them must not be symbolic links or junctions. If you see **Reparse point refused**, the message identifies the refused path; select the actual folder when the selected folder itself is a link. Wine/Proton testing is still in progress.

New installations put prepared assets under `<game>\rebirths-speedrun-patch\cg24-v1` by default. You can choose another location, including another drive. Existing installations keep their selected asset directory and profile. Installed DLC is included automatically. Running **Prepare / Resume** again checks existing output and rebuilds files that are missing or have changed. Do not put the output inside the game's `data` or `DLC` folders.

## Settings

Audio tail trimming starts on in Re;Birth1, Re;Birth2 and Re;Birth3, including
updates where the setting is missing. It shortens checked, near-silent endings
in Re;Birth1 Japanese/English voices and Re;Birth2 Japanese voices, and removes
checked, completely silent sound-effect endings in Re;Birth2/Re;Birth3.
Re;Birth2 English and Re;Birth3 voices stay unchanged. Sega Hard Girls is unaffected.

Original audio files stay intact. The patch changes playback metadata in memory;
checking the audio banks requires reading them at startup. Updates keep your
saved choice. Set `TrimSilentAudioTails=0` under `[Patches]` in
`rebirths-patches.ini` to disable it, or `1` to enable it. For Re;Birth3, the INI
is under the game's `Birth3\` folder. There is no preparer checkbox for this setting.

The voice checks cover 13,376 Re;Birth1 Japanese/English entries and 9,615
Re;Birth2 Japanese entries. Cuts leave a 20 ms margin after the console ending,
allow for alignment, and round outward to whole compressed audio blocks. The
retained audio is unchanged; removed samples are no louder than 2 units on the
16-bit PCM scale.

On a fresh install, prepared assets, **Fast texture conversion**, **Downscale
large story images**, and **Skip tutorials** are enabled. Story speedups are off;
Sega's **Fix dungeon movement** is on.

The downscale option halves the width and height of supported story illustrations
(CGs) whose uncompressed pixels take at least 24 MiB. It selects 12 Re;Birth1,
5 Re;Birth2 and 27 Re;Birth3 textures; Sega Hard Girls has no texture over the
threshold. Other large images stay at their original size and are reported
during preparation. The app loads your saved image-size choice for an existing
installation. Switching profiles selects a separate destination; it will not
overwrite a directory owned by another profile. Click **Prepare / Resume** to
apply your choices. If the preparer finds an unrecognized patch DLL, it leaves
that file alone and stops.

**Fast texture conversion** speeds up CPU texture decoding and layout conversion in all four games, with the same pixel output. It works with both original archives and prepared assets. Existing installs without this setting default to on; an explicit off setting is kept. To compare or disable it, uncheck the option and click **Install / Update**.

**Skip tutorials** bypasses automatically triggered tutorial screens and their skip confirmations in all four games. Re;Birth3 and Sega also bypass the intro that starts each tutorial block. Tutorials finish through the game's own script handling. Discovery state, scripted battles, rewards, unlocks and ordinary notices are kept, and the game stays at 60 FPS. Help opened from the menu remains usable. This setting works independently of **Auto-skip story (ADV)** and **Story fast-forward (ADV)**. If this setting is missing, it defaults to on. Updates keep it off if you previously disabled it. Uncheck **Skip tutorials** and click **Install / Update** to show automatic tutorials again. The INI equivalent is `[Patches] SkipTutorials=0`.

Re;Birth3 and Sega Hard Girls also have an optional face-texture optimization for character-switch
hitches. Close the game and set `FastFaceTextureCreation=1` under `[Patches]` in
the installed `rebirths-patches.ini` to enable it; set `0` to disable it. It keeps
the same face textures and 60 FPS. It defaults off and has no preparer
checkbox; updates keep your saved choice. Equipment tests with prepared assets
showed fewer long frames in Sega and a
small improvement in Re;Birth3's slowest frames. Re;Birth3 showed no consistent
improvement when loading from the original archives. Hitches can still occur.
Re;Birth1/Re;Birth2 do not have this wait and ignore the setting.

ADV refers to story scenes. **Story fast-forward (ADV)** speeds up their
presentation; **Auto-skip story (ADV)** activates the game's story skip. In Re;Birth3 it also
confirms the game's skip prompt for in-engine 3D cutscenes and enables the game's
I-key dialogue progression once per Nepstation event. Nepstation dialogue still
plays; press I to cancel its automatic progression. With **Skip tutorials** off,
close tutorials manually; story auto-skip resumes afterward.

Re;Birth3's separate **Skip Nepstation** checkbox skips all of Nepstation through
the game's confirmation and cleanup. It works independently of **Auto-skip story (ADV)**
and takes precedence when both are enabled. Both settings default off and work
with **Story fast-forward (ADV)** off. The INI equivalent is `[Patches] NepstationSkip=1`.

For the standard Re;Birth3 installation folder, the preparer puts the patch DLL, settings, and log in the `Birth3` subfolder automatically. Always select the folder containing the game EXE in the app.

## Backups and recovery

The preparer checks replacement files before installing them and saves backups under `<game>\rebirths-prepare-backups`. If you cancel during asset preparation, your installed patch and settings stay in place, and you can resume later.

**Rollback last update** restores the most recent verified patch DLL backup. It also restores the previous settings file if you have not edited the current one since the update. **Uninstall patch** removes a recognized patch DLL; it keeps settings, prepared assets, logs, backups, and the game EXE. Neither action deletes files the preparer cannot verify.

**Apply 4GB patch** is optional. It backs up a supported original EXE before patching it. A supported EXE that already has the 4GB patch is left unchanged. **Restore original EXE** is available only for patches applied by this preparer, and only while its backup and patched EXE still pass verification. **Uninstall patch** does not restore the EXE.

## What to test

Start with a familiar section of the game, then try your normal play. Each game has been tested on a limited English route, including reads from installed DLC, with and without the 4GB patch. Full playthroughs and other languages have not been tested. If something goes wrong, report the game, where it happened, installed DLC, selected settings, whether you applied the 4GB patch, and the exact error message. Keep game files and saves private unless the patch author asks for specific information.
