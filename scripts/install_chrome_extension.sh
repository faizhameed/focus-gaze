#!/usr/bin/env bash
# One-click wrapper: install focusGaze Bridge into every Chrome profile.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
exec python3 "$ROOT/scripts/chrome_extension_installer.py" "$@"
