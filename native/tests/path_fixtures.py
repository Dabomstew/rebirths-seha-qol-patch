"""Directory-link fixtures that need no Windows symlink privilege."""
import subprocess


def junction(link, target):
    subprocess.run(["cmd", "/c", "mklink", "/J", str(link), str(target)],
                   check=True, capture_output=True, text=True)
