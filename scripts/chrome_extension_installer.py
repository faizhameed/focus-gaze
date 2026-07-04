#!/usr/bin/env python3
"""Install the focusGaze Chrome extension for every Chrome profile (one-click).

Consumer Chrome (including recent versions) **blocks enabling** CRX / External
Extensions that are not from the Chrome Web Store — you see:

  "This extension is not listed in the Chrome Web Store and may have been
   added without your knowledge."

So the supported install path is:

1. Quit Chrome.
2. Remove any prior External Extensions CRX registration (that causes the block).
3. Copy the unpacked extension to a stable path under Application Support.
4. For **each** profile Preferences file: enable Developer Mode and register the
   extension as an **unpacked** load (location=4, state=enabled).
5. Optionally relaunch Chrome.

Unit tests cover ID derivation, profile discovery, and preference writing
without requiring Chrome.
"""

from __future__ import annotations

import argparse
import base64
import hashlib
import json
import os
import platform
import shutil
import subprocess
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import List, Optional, Sequence


# Default extension ID produced by extension/keys/focusgaze.pem (when present).
DEFAULT_EXTENSION_ID = "ocbhbndfchcjlkailmcmohpohjdclelg"

# Chromium Extension::Location::UNPACKED
LOCATION_UNPACKED = 4
# Extension::DISABLED = 0, ENABLED = 1
STATE_ENABLED = 1


@dataclass
class InstallResult:
    """Outcome of a multi-profile install attempt."""

    ok: bool
    extension_id: str
    install_path: str = ""
    crx_path: str = ""
    external_pref_path: str = ""
    profiles: List[str] = field(default_factory=list)
    profiles_updated: List[str] = field(default_factory=list)
    message: str = ""
    chrome_was_running: bool = False
    chrome_relaunched: bool = False
    errors: List[str] = field(default_factory=list)

    def to_dict(self) -> dict:
        return {
            "ok": self.ok,
            "extension_id": self.extension_id,
            "install_path": self.install_path,
            "crx_path": self.crx_path,
            "external_pref_path": self.external_pref_path,
            "profiles": list(self.profiles),
            "profiles_updated": list(self.profiles_updated),
            "message": self.message,
            "chrome_was_running": self.chrome_was_running,
            "chrome_relaunched": self.chrome_relaunched,
            "errors": list(self.errors),
        }


def extension_id_from_public_der(spki_der: bytes) -> str:
    """Compute Chrome extension ID from SubjectPublicKeyInfo DER bytes."""
    digest = hashlib.sha256(spki_der).digest()[:16]
    return "".join(chr(ord("a") + (b >> 4)) + chr(ord("a") + (b & 0x0F)) for b in digest)


def extension_id_from_pem(pem_path: Path) -> str:
    """Derive extension ID from an RSA private key PEM via openssl."""
    der = subprocess.check_output(
        ["openssl", "rsa", "-in", str(pem_path), "-pubout", "-outform", "DER"],
        stderr=subprocess.DEVNULL,
    )
    return extension_id_from_public_der(der)


def manifest_key_from_pem(pem_path: Path) -> str:
    """Base64 SPKI suitable for the Chrome manifest \"key\" field."""
    der = subprocess.check_output(
        ["openssl", "rsa", "-in", str(pem_path), "-pubout", "-outform", "DER"],
        stderr=subprocess.DEVNULL,
    )
    return base64.b64encode(der).decode("ascii")


def default_chrome_user_data_dir() -> Path:
    """Return the default Google Chrome user-data directory for this OS."""
    system = platform.system()
    home = Path.home()
    if system == "Darwin":
        return home / "Library/Application Support/Google/Chrome"
    if system == "Windows":
        local = os.environ.get("LOCALAPPDATA") or str(home / "AppData/Local")
        return Path(local) / "Google/Chrome/User Data"
    return home / ".config/google-chrome"


def discover_chrome_profiles(user_data_dir: Path) -> List[dict]:
    """List Chrome profiles from Local State info_cache (name + directory)."""
    profiles: List[dict] = []
    local_state = user_data_dir / "Local State"
    if local_state.is_file():
        try:
            data = json.loads(local_state.read_text(encoding="utf-8"))
            cache = (data.get("profile") or {}).get("info_cache") or {}
            for directory, meta in cache.items():
                profiles.append(
                    {
                        "dir": directory,
                        "name": (meta or {}).get("name") or directory,
                    }
                )
        except (OSError, json.JSONDecodeError, TypeError):
            profiles = []

    if profiles:
        return sorted(profiles, key=lambda p: p["dir"])

    if (user_data_dir / "Default").is_dir():
        profiles.append({"dir": "Default", "name": "Default"})
    for child in sorted(user_data_dir.glob("Profile *")):
        if child.is_dir():
            profiles.append({"dir": child.name, "name": child.name})
    return profiles


def external_extensions_dir(user_data_dir: Path) -> Path:
    """Chrome per-user External Extensions directory."""
    return user_data_dir / "External Extensions"


def remove_external_extension_pref(user_data_dir: Path, extension_id: str) -> Optional[Path]:
    """Delete External Extensions CRX registration (blocked on consumer Chrome)."""
    pref = external_extensions_dir(user_data_dir) / f"{extension_id}.json"
    if pref.is_file():
        pref.unlink()
        return pref
    return None


def write_external_extension_pref(
    user_data_dir: Path,
    extension_id: str,
    crx_path: Path,
    version: str,
) -> Path:
    """Legacy CRX external pref writer (kept for tests; not used for install)."""
    dest_dir = external_extensions_dir(user_data_dir)
    dest_dir.mkdir(parents=True, exist_ok=True)
    pref_path = dest_dir / f"{extension_id}.json"
    payload = {
        "external_crx": str(crx_path.resolve()),
        "external_version": version,
    }
    pref_path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
    return pref_path


def read_extension_version(extension_dir: Path) -> str:
    """Read version string from manifest.json (default 0.0.0)."""
    manifest = extension_dir / "manifest.json"
    try:
        data = json.loads(manifest.read_text(encoding="utf-8"))
        return str(data.get("version") or "0.0.0")
    except (OSError, json.JSONDecodeError):
        return "0.0.0"


def load_manifest(extension_dir: Path, pem_path: Optional[Path] = None) -> dict:
    """Load manifest.json and ensure stable public key is present when pem given."""
    manifest_path = extension_dir / "manifest.json"
    data = json.loads(manifest_path.read_text(encoding="utf-8"))
    if pem_path and pem_path.is_file():
        data["key"] = manifest_key_from_pem(pem_path)
    return data


def find_chrome_binary() -> Optional[Path]:
    """Locate the Google Chrome binary if present."""
    candidates = [
        Path("/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"),
        Path("/usr/bin/google-chrome"),
        Path("/usr/bin/google-chrome-stable"),
        Path(r"C:\Program Files\Google\Chrome\Application\chrome.exe"),
        Path(r"C:\Program Files (x86)\Google\Chrome\Application\chrome.exe"),
    ]
    which = shutil.which("google-chrome") or shutil.which("google-chrome-stable")
    if which:
        candidates.insert(0, Path(which))
    for path in candidates:
        if path.is_file():
            return path
    return None


def is_chrome_running() -> bool:
    """Best-effort check whether Chrome is running."""
    system = platform.system()
    try:
        if system == "Darwin":
            out = subprocess.check_output(
                ["pgrep", "-x", "Google Chrome"], stderr=subprocess.DEVNULL
            )
            return bool(out.strip())
        if system == "Windows":
            out = subprocess.check_output(
                ["tasklist", "/FI", "IMAGENAME eq chrome.exe"],
                stderr=subprocess.DEVNULL,
                text=True,
            )
            return "chrome.exe" in out.lower()
        out = subprocess.check_output(
            ["pgrep", "-f", "chrome"], stderr=subprocess.DEVNULL
        )
        return bool(out.strip())
    except (subprocess.CalledProcessError, FileNotFoundError, OSError):
        return False


def quit_chrome(timeout_sec: float = 20.0) -> bool:
    """Ask Chrome to quit gracefully. Returns True if no longer running."""
    system = platform.system()
    try:
        if system == "Darwin":
            subprocess.run(
                ["osascript", "-e", 'quit app "Google Chrome"'],
                check=False,
                capture_output=True,
            )
        elif system == "Windows":
            subprocess.run(
                ["taskkill", "/IM", "chrome.exe"],
                check=False,
                capture_output=True,
            )
        else:
            subprocess.run(["pkill", "-TERM", "-f", "chrome"], check=False)
    except OSError:
        pass

    deadline = time.time() + timeout_sec
    while time.time() < deadline:
        if not is_chrome_running():
            return True
        time.sleep(0.25)
    return not is_chrome_running()


def relaunch_chrome(chrome_bin: Optional[Path] = None) -> bool:
    """Start Chrome in the background."""
    chrome_bin = chrome_bin or find_chrome_binary()
    if not chrome_bin:
        return False
    try:
        if platform.system() == "Darwin":
            subprocess.Popen(
                ["open", "-a", "Google Chrome"],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
        else:
            subprocess.Popen(
                [str(chrome_bin)],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                start_new_session=True,
            )
        return True
    except OSError:
        return False


def durable_install_dir() -> Path:
    """User-local durable directory for the unpacked extension copy."""
    system = platform.system()
    home = Path.home()
    if system == "Darwin":
        base = home / "Library/Application Support/focusGaze"
    elif system == "Windows":
        appdata = os.environ.get("APPDATA") or str(home / "AppData/Roaming")
        base = Path(appdata) / "focusGaze"
    else:
        base = home / ".local/share/focusGaze"
    path = base / "chrome-extension-unpacked"
    path.mkdir(parents=True, exist_ok=True)
    return path


def sync_unpacked_extension(extension_dir: Path, dest_dir: Path, pem_path: Optional[Path]) -> Path:
    """Copy extension sources to a stable path and ensure manifest key is set."""
    if dest_dir.exists():
        shutil.rmtree(dest_dir)
    shutil.copytree(
        extension_dir,
        dest_dir,
        ignore=shutil.ignore_patterns("*.pem", ".DS_Store", "__pycache__", "*.crx"),
    )
    manifest_path = dest_dir / "manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if pem_path and pem_path.is_file():
        manifest["key"] = manifest_key_from_pem(pem_path)
        manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    return dest_dir


def _permissions_blob(manifest: dict) -> dict:
    """Build a permissions object Chrome expects in Preferences."""
    api = list(manifest.get("permissions") or [])
    hosts = list(manifest.get("host_permissions") or [])
    return {
        "api": api,
        "explicit_host": hosts,
        "manifest_permissions": [],
        "scriptable_host": [],
    }


def build_unpacked_extension_settings(
    extension_id: str,
    install_path: Path,
    manifest: dict,
) -> dict:
    """Preference entry for an unpacked (developer-mode) extension."""
    perms = _permissions_blob(manifest)
    # Chrome stores absolute path for unpacked extensions.
    path_str = str(install_path.resolve())
    return {
        "account_extension_type": 0,
        "active_permissions": perms,
        "commands": {},
        "content_settings": [],
        "creation_flags": 1,
        "disable_reasons": [],
        "from_webstore": False,
        "granted_permissions": perms,
        "incognito_content_settings": [],
        "incognito_preferences": {},
        "location": LOCATION_UNPACKED,
        "manifest": manifest,
        "path": path_str,
        "preferences": {},
        "regular_only_preferences": {},
        "state": STATE_ENABLED,
        "was_installed_by_default": False,
        "was_installed_by_oem": False,
        "withholding_permissions": False,
    }


def install_unpacked_into_profile(
    profile_dir: Path,
    extension_id: str,
    install_path: Path,
    manifest: dict,
) -> bool:
    """Enable developer mode and register the unpacked extension in one profile.

    Returns True if Preferences were updated. Chrome must be fully quit.
    """
    prefs_path = profile_dir / "Preferences"
    if not prefs_path.is_file():
        # Brand-new profile folder without Preferences yet — create minimal prefs.
        prefs: dict = {}
    else:
        try:
            prefs = json.loads(prefs_path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as exc:
            raise RuntimeError(f"Cannot read {prefs_path}: {exc}") from exc

    extensions = prefs.setdefault("extensions", {})
    ui = extensions.setdefault("ui", {})
    ui["developer_mode"] = True

    settings = extensions.setdefault("settings", {})
    # Drop any previous blocked external/CRX entry for this id.
    settings[extension_id] = build_unpacked_extension_settings(
        extension_id, install_path, manifest
    )

    # Clear toolbar pinning issues / force visibility is optional.
    # Write atomically.
    tmp = prefs_path.with_suffix(".focusgaze.tmp")
    tmp.write_text(json.dumps(prefs, separators=(",", ":"), ensure_ascii=False), encoding="utf-8")
    tmp.replace(prefs_path)
    return True


def resolve_repo_paths(repo_root: Optional[Path] = None) -> dict:
    """Resolve default extension/key paths relative to the focusGaze repo root."""
    if repo_root is None:
        repo_root = Path(__file__).resolve().parent.parent
    return {
        "repo_root": repo_root,
        "extension_dir": repo_root / "extension" / "chrome",
        "pem_path": repo_root / "extension" / "keys" / "focusgaze.pem",
    }


def install_extension(
    extension_dir: Path,
    pem_path: Path,
    user_data_dir: Optional[Path] = None,
    *,
    relaunch_chrome_flag: bool = True,
    quit_if_running: bool = True,
    chrome_bin: Optional[Path] = None,
) -> InstallResult:
    """One-click install of focusGaze Bridge into every Chrome profile (unpacked)."""
    extension_dir = Path(extension_dir)
    pem_path = Path(pem_path)
    user_data_dir = Path(user_data_dir) if user_data_dir else default_chrome_user_data_dir()
    errors: List[str] = []

    if not extension_dir.is_dir():
        return InstallResult(
            ok=False,
            extension_id="",
            message=f"Extension directory not found: {extension_dir}",
            errors=[f"missing_extension_dir:{extension_dir}"],
        )
    if not (extension_dir / "manifest.json").is_file():
        return InstallResult(
            ok=False,
            extension_id="",
            message=f"manifest.json missing in {extension_dir}",
            errors=["missing_manifest"],
        )
    if not user_data_dir.is_dir():
        return InstallResult(
            ok=False,
            extension_id="",
            message=f"Chrome user data directory not found: {user_data_dir}",
            errors=[f"missing_user_data:{user_data_dir}"],
        )

    # Prefer stable ID from PEM; fall back to manifest key / default constant.
    extension_id = DEFAULT_EXTENSION_ID
    if pem_path.is_file():
        try:
            extension_id = extension_id_from_pem(pem_path)
        except (subprocess.CalledProcessError, OSError) as exc:
            errors.append(f"pem_id_failed:{exc}")
    else:
        errors.append(f"missing_pem:{pem_path} (using default id)")

    profiles = discover_chrome_profiles(user_data_dir)
    profile_names = [p["name"] for p in profiles]
    was_running = is_chrome_running()

    if was_running and quit_if_running:
        if not quit_chrome():
            return InstallResult(
                ok=False,
                extension_id=extension_id,
                profiles=profile_names,
                chrome_was_running=was_running,
                message=(
                    "Chrome is still running. Quit Chrome completely "
                    "(Chrome menu → Quit Google Chrome), then run the installer again."
                ),
                errors=errors + ["chrome_quit_timeout"],
            )
    elif was_running and not quit_if_running:
        return InstallResult(
            ok=False,
            extension_id=extension_id,
            profiles=profile_names,
            chrome_was_running=True,
            message=(
                "Chrome must be fully quit to install an unpacked extension into "
                "all profiles. Quit Chrome, then re-run (without --no-quit)."
            ),
            errors=errors + ["chrome_still_running"],
        )

    # Remove blocked External CRX registration from earlier installer versions.
    removed = remove_external_extension_pref(user_data_dir, extension_id)
    if removed:
        errors.append(f"removed_blocked_external_pref:{removed}")

    install_path = durable_install_dir()
    try:
        sync_unpacked_extension(
            extension_dir, install_path, pem_path if pem_path.is_file() else None
        )
    except OSError as exc:
        return InstallResult(
            ok=False,
            extension_id=extension_id,
            profiles=profile_names,
            chrome_was_running=was_running,
            message=f"Failed to copy extension: {exc}",
            errors=errors + [str(exc)],
        )

    manifest = load_manifest(install_path, pem_path if pem_path.is_file() else None)
    updated: List[str] = []

    for prof in profiles:
        profile_path = user_data_dir / prof["dir"]
        if not profile_path.is_dir():
            errors.append(f"missing_profile_dir:{prof['dir']}")
            continue
        try:
            install_unpacked_into_profile(
                profile_path, extension_id, install_path, manifest
            )
            updated.append(prof["name"])
        except Exception as exc:  # noqa: BLE001
            errors.append(f"{prof['dir']}:{exc}")

    if not updated:
        return InstallResult(
            ok=False,
            extension_id=extension_id,
            install_path=str(install_path),
            profiles=profile_names,
            chrome_was_running=was_running,
            message="No profiles could be updated. " + "; ".join(errors),
            errors=errors,
        )

    relaunched = False
    if relaunch_chrome_flag:
        relaunched = relaunch_chrome(chrome_bin)

    msg = (
        f"Installed focusGaze Bridge as an unpacked developer extension "
        f"({extension_id}) into {len(updated)} profile(s): {', '.join(updated)}. "
        "Open chrome://extensions — Developer mode should be ON and the extension enabled. "
        "If it still shows disabled, click 'Load unpacked' once and choose the install path, "
        f"or re-enable the card. Install path: {install_path}"
    )
    return InstallResult(
        ok=True,
        extension_id=extension_id,
        install_path=str(install_path),
        profiles=profile_names,
        profiles_updated=updated,
        message=msg,
        chrome_was_running=was_running,
        chrome_relaunched=relaunched,
        errors=errors,
    )


def main(argv: Optional[Sequence[str]] = None) -> int:
    """CLI entry: install the extension for every Chrome profile (unpacked)."""
    parser = argparse.ArgumentParser(
        description=(
            "Install focusGaze Chrome extension into all profiles "
            "(unpacked / developer mode — works on consumer Chrome)"
        )
    )
    paths = resolve_repo_paths()
    parser.add_argument(
        "--extension-dir",
        type=Path,
        default=paths["extension_dir"],
        help="Path to unpacked extension directory",
    )
    parser.add_argument(
        "--pem",
        type=Path,
        default=paths["pem_path"],
        help="Path to RSA private key for stable extension ID",
    )
    parser.add_argument(
        "--user-data-dir",
        type=Path,
        default=None,
        help="Chrome user data directory (default: platform Chrome path)",
    )
    parser.add_argument(
        "--no-relaunch",
        action="store_true",
        help="Do not relaunch Chrome after install",
    )
    parser.add_argument(
        "--no-quit",
        action="store_true",
        help="Do not quit Chrome (install will refuse if Chrome is running)",
    )
    parser.add_argument(
        "--json",
        action="store_true",
        help="Print machine-readable JSON result",
    )
    parser.add_argument(
        "--print-id",
        action="store_true",
        help="Only print the extension ID derived from --pem and exit",
    )
    parser.add_argument(
        "--remove-blocked-only",
        action="store_true",
        help="Only remove the blocked External Extensions CRX registration and exit",
    )
    args = parser.parse_args(argv)

    if args.print_id:
        print(extension_id_from_pem(args.pem))
        return 0

    user_data = args.user_data_dir or default_chrome_user_data_dir()
    if args.remove_blocked_only:
        ext_id = DEFAULT_EXTENSION_ID
        if args.pem.is_file():
            try:
                ext_id = extension_id_from_pem(args.pem)
            except Exception:  # noqa: BLE001
                pass
        removed = remove_external_extension_pref(user_data, ext_id)
        if args.json:
            print(json.dumps({"ok": True, "removed": str(removed) if removed else None}))
        else:
            print(f"Removed blocked external pref: {removed}" if removed else "Nothing to remove")
        return 0

    result = install_extension(
        args.extension_dir,
        args.pem,
        args.user_data_dir,
        relaunch_chrome_flag=not args.no_relaunch,
        quit_if_running=not args.no_quit,
    )
    if args.json:
        print(json.dumps(result.to_dict(), indent=2))
    else:
        print(result.message)
        if result.errors:
            for err in result.errors:
                print(f"note: {err}", file=sys.stderr)
    return 0 if result.ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
