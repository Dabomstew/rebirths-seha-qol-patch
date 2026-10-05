"""Read shipped INI settings and check a feature's own defaults."""
import configparser
from pathlib import Path


CONFIG = Path(__file__).resolve().parents[1] / "rebirths-patches.ini"


def parse_config(text):
    config = configparser.ConfigParser(interpolation=None)
    config.optionxform = str
    config.read_string(text)
    return config


def read_config(path=CONFIG):
    return parse_config(path.read_text(encoding="utf-8"))


def check_feature_defaults(config, expected):
    for key, value in expected.items():
        actual = config.get("Patches", key, fallback=None)
        if actual != value:
            raise AssertionError(f"[Patches] {key}: expected {value!r}, got {actual!r}")
