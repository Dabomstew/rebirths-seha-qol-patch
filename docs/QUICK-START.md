# Re;Birth release quick start

1. Close the game. Extract the ZIP to a folder on a 64-bit Windows PC.
2. Run `Rebirths-Preparer.exe`. Select a detected game, or click **Game folder...** and choose the folder containing its EXE.
3. Choose where to save the prepared assets. New installs default to `<game>\rebirths-speedrun-patch\cg24-v1`; you can use another drive. Leave **Fast texture conversion** checked; it starts on unless you previously disabled it. Leave **Use prepared assets** checked for your first install. **Downscale large story images** starts on for new installs and halves the width and height of supported story illustrations (CGs) whose uncompressed pixels take at least 24 MiB.
4. Click **Prepare / Resume** and wait for it to finish. Preparation needs extra disk space and may take a while. If you cancel, click **Prepare / Resume** again to continue.
5. Click **Play**, or launch the game normally. **Play** saves the displayed settings before starting the game. Leave story speedups off for the first test. Tell us which game and scene you tried, which DLC you have installed, and what happened.

The preparer supports Re;Birth1, Re;Birth2, Re;Birth3, and Neptune VS Sega Hard Girls. It accepts supported versions of the game EXE and patch DLL. If it refuses either file, leave it in place and report the exact message.

Re;Birth1, Re;Birth2 and Re;Birth3 trim checked quiet or silent audio endings by default. This covers near-silent voice endings in Re;Birth1 Japanese/English and Re;Birth2 Japanese, plus completely silent sound-effect endings in Re;Birth2/Re;Birth3. Re;Birth2 English voices stay unchanged. Updates keep your saved choice. To disable it, set `TrimSilentAudioTails=0` under `[Patches]` in `rebirths-patches.ini` (under `Birth3\` for Re;Birth3).

**Apply 4GB patch** is optional and runs separately. **Restore original EXE** undoes a 4GB patch applied by this preparer. **Rollback last update** restores the latest verified patch DLL backup. **Uninstall patch** removes the recognized patch DLL but keeps settings, prepared assets, logs, and backups. See `USER-GUIDE.md` for details.

**Install / Update** saves settings and installs the patch without preparing assets. Assets without prepared copies use the original archives. Use **Prepare / Resume** when changing the prepared-assets location or image-size profile.

**Skip tutorials** starts on in all four games. It bypasses automatic tutorial screens and confirmations; Re;Birth3 and Sega also bypass the tutorial intro. Help pages opened from the menu remain available. Uncheck it and click **Install / Update** to keep automatic tutorials. Updates keep this off if you previously disabled it.
