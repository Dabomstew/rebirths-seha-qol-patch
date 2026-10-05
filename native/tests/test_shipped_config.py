"""Independent contract for the shipped INI, plus configuration regressions.

This oracle is intentionally maintained separately from runtime/preparer metadata.
It describes the shipped runtime INI, not fresh-preparer or upgrade defaults.
"""
import configparser
import unittest

from shipped_config import check_feature_defaults, parse_config, read_config


EXPECTED_CONFIG = {
    "Patches": {
        "TrimSilentAudioTails": "1",
        "FastTextureConversion": "1",
        "FastFaceTextureCreation": "0",
        "AdvFastForward": "0",
        "AdvAutoSkip": "0",
        "NepstationSkip": "0",
        "SkipTutorials": "1",
        "BattleLoadDelaySkip": "0",
        "SkipChapterIntros": "0",
        "DungeonMovementFix": "1",
        "UncompressedAssets": "0",
    },
    "UncompressedAssets": {
        "Directory": "rebirths-speedrun-patch\\assets",
        "Verify": "0",
    },
}


def check_shipped_contract(config):
    if config.defaults():
        raise AssertionError("shipped configuration must not inherit DEFAULT settings")
    if set(config.sections()) != set(EXPECTED_CONFIG):
        raise AssertionError(f"unexpected shipped sections: {config.sections()!r}")
    for section, expected in EXPECTED_CONFIG.items():
        actual = dict(config[section])
        if actual != expected:
            raise AssertionError(f"[{section}]: expected {expected!r}, got {actual!r}")


class ShippedConfigTest(unittest.TestCase):
    def test_shipped_contract(self):
        check_shipped_contract(read_config())

    def test_every_changed_default_is_rejected(self):
        for section, settings in EXPECTED_CONFIG.items():
            for key, value in settings.items():
                with self.subTest(section=section, key=key):
                    config = read_config()
                    config[section][key] = "1" if value == "0" else "0"
                    with self.assertRaises(AssertionError):
                        check_shipped_contract(config)

    def test_every_missing_key_is_rejected(self):
        for section, settings in EXPECTED_CONFIG.items():
            for key in settings:
                with self.subTest(section=section, key=key):
                    config = read_config()
                    del config[section][key]
                    with self.assertRaises(AssertionError):
                        check_shipped_contract(config)

    def test_unknown_keys_and_sections_are_rejected(self):
        for section in EXPECTED_CONFIG:
            with self.subTest(section=section):
                config = read_config()
                config[section]["UnsupportedSetting"] = "0"
                with self.assertRaises(AssertionError):
                    check_shipped_contract(config)
        config = read_config()
        config.add_section("UnsupportedSection")
        with self.assertRaises(AssertionError):
            check_shipped_contract(config)

    def test_missing_sections_and_default_inheritance_are_rejected(self):
        for section in EXPECTED_CONFIG:
            with self.subTest(section=section):
                config = read_config()
                config.remove_section(section)
                with self.assertRaises(AssertionError):
                    check_shipped_contract(config)
        config = read_config()
        del config["Patches"]["AdvAutoSkip"]
        config["DEFAULT"]["AdvAutoSkip"] = "0"
        with self.assertRaises(AssertionError):
            check_shipped_contract(config)

    def test_duplicate_keys_are_rejected(self):
        with self.assertRaises(configparser.DuplicateOptionError):
            parse_config("[Patches]\nAdvAutoSkip=0\nAdvAutoSkip=1\n")

    def test_feature_checks_accept_unrelated_keys(self):
        config = read_config()
        config["Patches"]["FutureUnrelatedFeature"] = "1"
        check_feature_defaults(config, {"AdvFastForward": "0", "AdvAutoSkip": "0"})
        with self.assertRaises(AssertionError):
            check_shipped_contract(config)

    def test_feature_checks_reject_changed_or_missing_owned_keys(self):
        for key in ("AdvFastForward", "AdvAutoSkip", "NepstationSkip", "SkipChapterIntros"):
            for value in (None, "1"):
                with self.subTest(key=key, value=value):
                    config = read_config()
                    if value is None:
                        del config["Patches"][key]
                    else:
                        config["Patches"][key] = value
                    with self.assertRaisesRegex(AssertionError, key):
                        check_feature_defaults(config, {key: "0"})


if __name__ == "__main__":
    unittest.main()
