"""Run the shipped-INI contract and all three Re;Birth ADV static verifiers.

No game launch or writes. Supply --baseline-dir or the REBIRTH{1,2,3}_GAME_EXE
environment variables. This focused suite does not run the native hook fixtures.
"""
import argparse
import os
from pathlib import Path
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline-dir", type=Path,
                        help="directory containing rebirth1/, rebirth2/, rebirth3/ baselines")
    args = parser.parse_args()
    tests = Path(__file__).resolve().parent
    if subprocess.run([sys.executable, str(tests.parent / "generate_catalog.py"), "--check"]).returncode:
        return 1
    env = os.environ.copy()
    if args.baseline_dir is not None:
        for game in (1, 2, 3):
            env[f"REBIRTH{game}_GAME_EXE"] = str(
                args.baseline_dir.resolve() / f"rebirth{game}" / f"NeptuniaReBirth{game}.exe")

    scripts = ["test_shipped_config.py"] + [
        f"rebirth{game}_adv_fast_forward_static_test.py" for game in (1, 2, 3)
    ]
    failed = []
    for script in scripts:
        print(f"Running {script}", flush=True)
        if subprocess.run([sys.executable, str(tests / script)], env=env).returncode:
            failed.append(script)
    if failed:
        print(f"FAILED: {', '.join(failed)}", file=sys.stderr)
        return 1
    print("Shipped configuration and three Re;Birth ADV static checks passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
