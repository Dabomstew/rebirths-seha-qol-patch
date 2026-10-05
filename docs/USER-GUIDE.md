# Re;Birth release preparer

## Games and asset location

This app prepares game assets and installs the patch for Re;Birth1, Re;Birth2, Re;Birth3, or Neptune VS Sega Hard Girls. It requires 64-bit Windows. Select a detected Steam installation, or click **Browse** and choose the folder containing the game EXE. The preparer checks that your game version is supported before making changes. It leaves the original game archives intact.

For Re;Birth2, the preparer recognizes both cataloged `GAME00001.pac` versions, including the version with the repaired help texture. It refuses an unknown source version before installing anything.

The game and output folders can be reached through linked parent folders, such as a Steam library path on Linux. The selected folders themselves and files or subfolders inside them must not be symbolic links or junctions. If you see **Reparse point refused**, the message identifies the refused path; select the actual folder when the selected folder itself is a link. Wine/Proton testing is still in progress.

New installations put prepared assets under `<game>\rebirths-speedrun-patch\cg24-v1` by default. You can choose another location, including another drive. Existing installations keep their selected asset directory and profile. Installed DLC is included automatically. Running **Prepare / Resume** again checks existing output and rebuilds files that are missing or have changed. Do not put the output inside the game's `data` or `DLC` folders.

## Settings

Re;Birth1, Re;Birth2 and Re;Birth3 enable audio tail trimming by default, including updates where the setting is missing. It shortens qualified near-silent voice endings in 13,376 Re;Birth1 Japanese/English entries and 9,615 Re;Birth2 Japanese entries. Voice cuts retain the console endpoint, alignment allowance and 20ms guard, round outward to complete ADPCM blocks, and preserve every PC sample above 3 PCM16 LSB. Retained decoded samples are identical; removed quiet samples peak at 2 PCM16 LSB. Re;Birth2 English and Re;Birth3 voices stay unchanged. Full source-bank verification adds a bank read at startup.

The same option removes verified digital-silent sound-effect packets in Re;Birth2/Re;Birth3. Both adapters correct private metadata while keeping original bank files and compressed audio intact. Explicit choices survive updates. This option is configured in `rebirths-patches.ini`: set `TrimSilentAudioTails=0` under `[Patches]` to disable it, or `1` to enable it. For Re;Birth3, the INI is under the game's `Birth3\` folder. Sega Hard Girls is unaffected.

On a fresh install, prepared assets, **Fast texture conversion**, **Downscale large ADV CGs**, and **Skip tutorials** are enabled. ADV speedups are off. Sega's dungeon movement fix is on. The downscale option half-sizes the complete image of a cataloged ADV CG TID when its original RGBA pixel payload is at least 24 MiB. It currently selects 12 Re;Birth1, 5 Re;Birth2 and 27 Re;Birth3 textures; Sega Hard Girls has no texture over the threshold. Unknown large assets stay at their original size and are reported during preparation. The app loads the active profile for an existing installation. Switching profiles selects a separate destination; it will not overwrite a directory owned by another profile. Click **Prepare / Resume** to apply your choices. If the preparer finds an unrecognized patch DLL, it leaves that file alone and stops.

**Fast texture conversion** speeds up CPU texture decoding and layout conversion in all four games, with the same pixel output. It works with both original archives and prepared assets. Existing installs without this setting default to on; an explicit off setting is kept. To compare or disable it, uncheck the option and click **Prepare / Resume**.

**Skip tutorials** bypasses automatically triggered tutorial screens and their skip confirmations in all four games. Re;Birth3 and Sega also bypass the intro that starts each tutorial block. The plugin uses native script completion and preserves discovery state, scripted battles, rewards, unlocks, ordinary notices, and 60 FPS pacing. It sends no dismissal inputs. Help opened from the menu remains usable. This setting works independently of **ADV auto-skip** and **ADV fast-forward**. Missing settings default to on; explicit off choices survive updates. Uncheck **Skip tutorials** and click **Prepare / Resume** to show automatic tutorials again. The INI equivalent is `[Patches] SkipTutorials=0`.

Re;Birth3 and Sega Hard Girls also have an optional face-texture optimization for character-switch
hitches. Close the game and set `FastFaceTextureCreation=1` under `[Patches]` in
the installed `rebirths-patches.ini` to enable it; set `0` to disable it. It keeps
native face textures and 60 FPS pacing. It defaults off and has no preparer
checkbox; updates preserve your explicit setting. Prepared-assets equipment tests
show fewer long frames for Sega; Re;Birth3 has a modest prepared-assets p99
improvement and no consistent archive p99 gain. Presentation stalls can still occur.
Re;Birth1/Re;Birth2 already lack the corresponding wait and ignore this setting.

**ADV auto-skip** activates the game's native story skip. In Re;Birth3 it also
confirms the native skip prompt for in-engine 3D cutscenes and enables native
I-key dialogue progression once per Nepstation event. Nepstation dialogue still
plays; press I to cancel its automatic progression. With **Skip tutorials** off,
close tutorials manually; story auto-skip resumes afterward.

Re;Birth3's separate **Skip Nepstation** checkbox fully skips Nepstation through
the native confirmation and cleanup. It works independently of **ADV auto-skip**
and takes precedence when both are enabled. Both settings default off and work
with **ADV fast-forward** off. The INI equivalent is `[Patches] NepstationSkip=1`.

For the standard Re;Birth3 installation folder, the preparer puts the patch DLL, settings, and log in the `Birth3` subfolder automatically. Always select the folder containing the game EXE in the app.

## Backups and recovery

The preparer checks replacement files before installing them and saves backups under `<game>\rebirths-prepare-backups`. If you cancel during asset preparation, your installed patch and settings stay in place, and you can resume later.

**Rollback proxy update** restores the most recent verified patch DLL backup. It also restores the previous settings file if you have not edited the current one since the update. **Uninstall proxy** removes a recognized patch DLL; it keeps settings, prepared assets, logs, backups, and the game EXE. Neither action deletes files the preparer cannot verify.

**Apply 4GB patch** is optional. It backs up a supported original EXE before patching it. A supported EXE that already has the 4GB patch is left unchanged. **Restore original EXE** is available only for patches applied by this preparer, and only while its backup and patched EXE still pass verification. **Uninstall proxy** does not restore the EXE.

## What to test

Start with a familiar section of the game, then try your normal play. Each game has been tested on a limited English route, including reads from installed DLC, with and without the 4GB patch. Full playthroughs and other languages have not been tested. If something goes wrong, report the game, where it happened, installed DLC, selected settings, whether you applied the 4GB patch, and the exact error message. Keep game files and saves private unless the patch author asks for specific information.
