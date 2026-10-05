"""Formatting independence and immutable-instruction mismatch contracts."""
import copy
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from unittest.mock import patch
from compiled_profiles import NATIVE, profiles
from texture_conversion_static_test import validate

class ProfileTests(unittest.TestCase):
    def test_formatting_and_literal_spelling_do_not_change_profiles(self):
        expected = profiles()
        with tempfile.TemporaryDirectory(prefix='profile contract ', dir=NATIVE.parent / 'build') as temporary:
            root = Path(temporary)
            include = root / 'include'
            shutil.copytree(NATIVE / 'include', include)
            for filename in ('texture_conversion_profiles.hpp', 'face_texture_profiles.hpp', 'tutorial_profiles.hpp'):
                path = include / filename
                # Break the old line/brace/lowercase-hex regex contracts, keeping C++ values.
                path.write_text(path.read_text().replace('0x', '0X').replace(', {', ',\n {').replace('Profile{', 'Profile\n{'))
            result = subprocess.run([str(NATIVE / 'build-profile-probe.cmd'), str(include), str(root / 'out')], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            actual = json.loads(subprocess.check_output([str(root / 'out/profile-probe.exe')], text=True))
            self.assertEqual(actual['games'], expected)

    def test_profile_value_mismatch_is_detected_against_baseline(self):
        changed = copy.deepcopy(profiles())
        changed['rebirth1']['texture']['helpers'][0]['rva'] += 1
        with patch('texture_conversion_static_test.profiles', return_value=changed):
            with self.assertRaises(AssertionError): validate('rebirth1')

if __name__ == '__main__': unittest.main()
