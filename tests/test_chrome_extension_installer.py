"""Unit tests for the multi-profile Chrome extension installer."""

from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))

from chrome_extension_installer import (  # noqa: E402
    build_unpacked_extension_settings,
    discover_chrome_profiles,
    extension_id_from_public_der,
    install_unpacked_into_profile,
    read_extension_version,
    remove_external_extension_pref,
    write_external_extension_pref,
)


class ExtensionIdTests(unittest.TestCase):
    def test_extension_id_is_32_chars_a_to_p(self):
        der = bytes(range(32)) + b"\x00" * 32
        ext_id = extension_id_from_public_der(der)
        self.assertEqual(len(ext_id), 32)
        self.assertTrue(all("a" <= c <= "p" for c in ext_id))

    def test_extension_id_stable(self):
        der = b"\x01\x02\x03" + bytes(100)
        self.assertEqual(
            extension_id_from_public_der(der),
            extension_id_from_public_der(der),
        )


class ProfileDiscoveryTests(unittest.TestCase):
    def test_discover_from_local_state(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "Default").mkdir()
            (root / "Profile 1").mkdir()
            local = {
                "profile": {
                    "info_cache": {
                        "Default": {"name": "Person 1"},
                        "Profile 1": {"name": "Work"},
                    }
                }
            }
            (root / "Local State").write_text(json.dumps(local), encoding="utf-8")
            profiles = discover_chrome_profiles(root)
            names = {p["name"] for p in profiles}
            self.assertEqual(names, {"Person 1", "Work"})

    def test_discover_fallback_scan(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "Default").mkdir()
            (root / "Profile 2").mkdir()
            profiles = discover_chrome_profiles(root)
            dirs = {p["dir"] for p in profiles}
            self.assertIn("Default", dirs)
            self.assertIn("Profile 2", dirs)


class ExternalPrefTests(unittest.TestCase):
    def test_write_and_remove_external_pref(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            crx = root / "ext.crx"
            crx.write_bytes(b"crx-bytes")
            ext_id = "abcdefghijklmnopabcdefghijklmnop"
            pref = write_external_extension_pref(root, ext_id, crx, "1.2.3")
            self.assertTrue(pref.is_file())
            removed = remove_external_extension_pref(root, ext_id)
            self.assertEqual(removed, pref)
            self.assertFalse(pref.is_file())


class UnpackedPrefsTests(unittest.TestCase):
    def test_build_settings_location_unpacked(self):
        manifest = {
            "manifest_version": 3,
            "name": "focusGaze Bridge",
            "version": "0.3.0",
            "permissions": ["tabs", "storage"],
            "host_permissions": ["http://127.0.0.1/*"],
        }
        settings = build_unpacked_extension_settings(
            "abcdefghijklmnopabcdefghijklmnop",
            Path("/tmp/ext"),
            manifest,
        )
        self.assertEqual(settings["location"], 4)
        self.assertEqual(settings["state"], 1)
        self.assertEqual(settings["disable_reasons"], [])
        self.assertFalse(settings["from_webstore"])

    def test_install_into_profile_sets_developer_mode(self):
        with tempfile.TemporaryDirectory() as tmp:
            profile = Path(tmp) / "Default"
            profile.mkdir()
            (profile / "Preferences").write_text("{}", encoding="utf-8")
            manifest = {
                "manifest_version": 3,
                "name": "focusGaze Bridge",
                "version": "0.3.0",
                "permissions": ["tabs", "storage"],
                "host_permissions": ["http://127.0.0.1/*"],
            }
            install_path = Path(tmp) / "ext"
            install_path.mkdir()
            (install_path / "manifest.json").write_text(json.dumps(manifest), encoding="utf-8")
            ext_id = "abcdefghijklmnopabcdefghijklmnop"
            install_unpacked_into_profile(profile, ext_id, install_path, manifest)
            prefs = json.loads((profile / "Preferences").read_text(encoding="utf-8"))
            self.assertTrue(prefs["extensions"]["ui"]["developer_mode"])
            self.assertIn(ext_id, prefs["extensions"]["settings"])
            self.assertEqual(prefs["extensions"]["settings"][ext_id]["location"], 4)
            self.assertEqual(prefs["extensions"]["settings"][ext_id]["state"], 1)


class ManifestVersionTests(unittest.TestCase):
    def test_read_version(self):
        with tempfile.TemporaryDirectory() as tmp:
            d = Path(tmp)
            (d / "manifest.json").write_text(
                json.dumps({"manifest_version": 3, "version": "0.3.0"}),
                encoding="utf-8",
            )
            self.assertEqual(read_extension_version(d), "0.3.0")

    def test_missing_manifest(self):
        with tempfile.TemporaryDirectory() as tmp:
            self.assertEqual(read_extension_version(Path(tmp)), "0.0.0")


if __name__ == "__main__":
    unittest.main()
