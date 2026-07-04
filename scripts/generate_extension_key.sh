#!/usr/bin/env bash
# Generate (or reuse) the RSA key that pins the Chrome extension ID.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
KEY_DIR="$ROOT/extension/keys"
PEM="$KEY_DIR/focusgaze.pem"
mkdir -p "$KEY_DIR"
if [[ ! -f "$PEM" ]]; then
  openssl genrsa 2048 > "$PEM"
  chmod 600 "$PEM"
  echo "Generated $PEM"
else
  echo "Using existing $PEM"
fi
ID="$(python3 "$ROOT/scripts/chrome_extension_installer.py" --pem "$PEM" --print-id)"
echo "$ID" > "$KEY_DIR/extension_id.txt"
echo "Extension ID: $ID"
# Refresh manifest public key field
python3 - << PY
import json, subprocess, base64
from pathlib import Path
root = Path("$ROOT")
pem = root / "extension/keys/focusgaze.pem"
manifest_path = root / "extension/chrome/manifest.json"
der = subprocess.check_output(
    ["openssl", "rsa", "-in", str(pem), "-pubout", "-outform", "DER"],
    stderr=subprocess.DEVNULL,
)
key = base64.b64encode(der).decode()
data = json.loads(manifest_path.read_text())
data["key"] = key
manifest_path.write_text(json.dumps(data, indent=2) + "\n")
print("Updated manifest key field")
PY
