# Patch configuration

Edit `rebirths-patches.ini` beside the installed `X3DAudio1_7.dll`, then restart
the game. For a Re;Birth3 folder ending in `Re;Birth3`, these files are in its
`Birth3` subfolder. Keep your existing sections and other settings.

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
