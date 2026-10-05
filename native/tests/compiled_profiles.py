"""Read the actual compiled profile values, independent of C++ spelling/layout."""
from functools import lru_cache
import json
from pathlib import Path
import subprocess

NATIVE = Path(__file__).resolve().parents[1]

@lru_cache(maxsize=1)
def profiles():
    # Always compile once per verifier process. No stale checked-in/generated oracle.
    result = subprocess.run([str(NATIVE / 'build-profile-probe.cmd')],
                            cwd=NATIVE.parent, capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError('profile probe build failed\n' + result.stdout + result.stderr)
    output = subprocess.check_output([str(NATIVE.parent / 'build/profile-probe/profile-probe.exe')], text=True)
    value = json.loads(output)
    assert value['schema'] == 1
    assert set(value['games']) == {'rebirth1', 'rebirth2', 'rebirth3', 'sega-hard-girls'}
    return value['games']
