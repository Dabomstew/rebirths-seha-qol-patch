# Re;Birth release quick start

1. Close the game. Extract the ZIP to a folder on a 64-bit Windows PC.
2. Run `Rebirths-Preparer.exe`. Select a detected game, or click **Browse** and choose the folder containing its EXE.
3. Choose where to save the prepared assets. New installs default to `<game>\rebirths-speedrun-patch\cg24-v1`; you can use another drive. Leave **Fast texture conversion** checked; it starts on unless you previously disabled it. Leave **Use prepared assets** checked for your first install. **Downscale large ADV CGs** starts on for new installs and half-sizes cataloged CG textures of at least 24 MiB.
4. Click **Prepare / Resume** and wait for it to finish. Preparation needs extra disk space and may take a while. If you cancel, click **Prepare / Resume** again to continue.
5. Launch the game normally. Leave ADV speedups off for the first test. Tell us which game and scene you tried, which DLC you have installed, and what happened.

The preparer supports Re;Birth1, Re;Birth2, Re;Birth3, and Neptune VS Sega Hard Girls. It accepts supported versions of the game EXE and patch DLL. If it refuses either file, leave it in place and report the exact message.

Re;Birth1, Re;Birth2 and Re;Birth3 trim qualified audio tails by default. This covers near-silent voice endings in Re;Birth1 Japanese/English and Re;Birth2 Japanese, plus digital-silent sound-effect endings in Re;Birth2/Re;Birth3. Re;Birth2 English voices stay unchanged. Updates preserve an explicit choice. To disable it, set `TrimSilentAudioTails=0` under `[Patches]` in `rebirths-patches.ini` (under `Birth3\` for Re;Birth3).

**Apply 4GB patch** is optional and runs separately. **Restore original EXE** undoes a 4GB patch applied by this preparer. **Rollback proxy update** restores the latest verified patch DLL backup. **Uninstall proxy** removes the recognized patch DLL but keeps settings, prepared assets, logs, and backups. See `USER-GUIDE.md` for details.

**Skip tutorials** starts on in all four games. It bypasses automatic tutorial screens and confirmations; Re;Birth3 and Sega also bypass the tutorial intro. Help pages opened from the menu remain available. Uncheck it and click **Prepare / Resume** to keep automatic tutorials. Updates preserve an explicit off setting.
