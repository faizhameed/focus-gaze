#!/usr/bin/env bash
# Phase 5: Build a relocatable focusGaze.app + DMG for macOS.
# - macdeployqt bundles Qt frameworks
# - copies OpenCV / ONNX Runtime dylibs when linked
# - includes focusgaze-nm-host for Chrome Native Messaging
# - optional Developer ID sign + notarize when credentials are set
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# Dedicated package build dir so we don't clobber the dev `build/` CMake cache.
BUILD_DIR="${BUILD_DIR:-$ROOT/build-package}"
DIST_DIR="${DIST_DIR:-$ROOT/dist}"
APP_NAME="focusGaze"
APP_PATH="$BUILD_DIR/${APP_NAME}.app"
VERSION="$(grep -E 'project\(focusGaze VERSION' "$ROOT/CMakeLists.txt" | sed -E 's/.*VERSION ([0-9.]+).*/\1/')"
DMG_PATH="$DIST_DIR/${APP_NAME}-${VERSION}.dmg"

# Optional: export SIGN_IDENTITY="Developer ID Application: Your Name (TEAMID)"
# Optional notarization: APPLE_ID, APPLE_TEAM_ID, APPLE_APP_SPECIFIC_PASSWORD (or NOTARY_PROFILE)

echo "==> Phase 5 package: focusGaze ${VERSION}"
echo "==> Configuring & building GUI bundle (BUILD_DIR=$BUILD_DIR)"
cmake -S "$ROOT" -B "$BUILD_DIR" \
  -DCMAKE_BUILD_TYPE=Release \
  -DFOCUSGAZE_BUILD_GUI=ON \
  -DFOCUSGAZE_BUILD_TESTS=OFF
cmake --build "$BUILD_DIR" --target focusGaze_gui focusgaze_nm_host -j"$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"

if [[ ! -d "$APP_PATH" ]]; then
  echo "error: expected app at $APP_PATH" >&2
  exit 1
fi

MACOS_DIR="$APP_PATH/Contents/MacOS"
FRAMEWORKS_DIR="$APP_PATH/Contents/Frameworks"
mkdir -p "$FRAMEWORKS_DIR" "$APP_PATH/Contents/Resources"

# Ensure NM host is present next to the main binary (CMake POST_BUILD also copies it).
if [[ -x "$BUILD_DIR/focusgaze-nm-host" ]]; then
  cp -f "$BUILD_DIR/focusgaze-nm-host" "$MACOS_DIR/focusgaze-nm-host"
  chmod +x "$MACOS_DIR/focusgaze-nm-host"
fi
if [[ ! -x "$MACOS_DIR/focusgaze-nm-host" ]]; then
  echo "warning: focusgaze-nm-host missing from bundle — Native Messaging install will fail until fixed" >&2
fi

# App icon (also wired via CMake; re-copy for safety).
if [[ -f "$ROOT/resources/AppIcon.icns" ]]; then
  cp -f "$ROOT/resources/AppIcon.icns" "$APP_PATH/Contents/Resources/AppIcon.icns"
fi

# --- Qt frameworks (required for install on machines without Homebrew Qt) ---
MACDEPLOYQT=""
for candidate in \
  "$(brew --prefix qtbase 2>/dev/null)/bin/macdeployqt" \
  "$(brew --prefix qt 2>/dev/null)/bin/macdeployqt" \
  "/opt/homebrew/opt/qtbase/bin/macdeployqt" \
  "/opt/homebrew/opt/qt/bin/macdeployqt" \
  "/usr/local/opt/qt/bin/macdeployqt"
do
  if [[ -n "$candidate" && -x "$candidate" ]]; then
    MACDEPLOYQT="$candidate"
    break
  fi
done

if [[ -n "$MACDEPLOYQT" ]]; then
  echo "==> macdeployqt: $MACDEPLOYQT"
  # Do not use -dmg here; we create a custom DMG with Applications link below.
  "$MACDEPLOYQT" "$APP_PATH" -always-overwrite || {
    echo "warning: macdeployqt returned non-zero; continuing" >&2
  }
else
  echo "warning: macdeployqt not found — app may only run on this machine (Homebrew Qt required)" >&2
fi

# --- Bundle commonly linked third-party dylibs into Frameworks/ and fix install names ---
copy_and_relocate_dylib() {
  local libpath="$1"
  [[ -f "$libpath" ]] || return 0
  local base
  base="$(basename "$libpath")"
  local dest="$FRAMEWORKS_DIR/$base"
  if [[ ! -f "$dest" ]]; then
    echo "    bundling $base"
    cp -f "$libpath" "$dest"
    chmod u+w "$dest" 2>/dev/null || true
  fi
  # Point main binary (and nm host) at @executable_path/../Frameworks
  if [[ -x "$MACOS_DIR/focusGaze" ]]; then
    install_name_tool -change "$libpath" "@executable_path/../Frameworks/$base" "$MACOS_DIR/focusGaze" 2>/dev/null || true
  fi
  if [[ -x "$MACOS_DIR/focusgaze-nm-host" ]]; then
    install_name_tool -change "$libpath" "@executable_path/../Frameworks/$base" "$MACOS_DIR/focusgaze-nm-host" 2>/dev/null || true
  fi
}

echo "==> Collecting non-system dylib dependencies"
if command -v otool >/dev/null 2>&1 && [[ -x "$MACOS_DIR/focusGaze" ]]; then
  # First-level deps of the main binary that live under /opt/homebrew or /usr/local.
  while IFS= read -r line; do
    lib="${line##* }"
    lib="${lib%% (*}"
    case "$lib" in
      /opt/homebrew/*|/usr/local/*)
        copy_and_relocate_dylib "$lib"
        ;;
    esac
  done < <(otool -L "$MACOS_DIR/focusGaze" | tail -n +2)
fi

# Ad-hoc or Developer ID codesign (must be after dylib rewrites).
if [[ -n "${SIGN_IDENTITY:-}" ]]; then
  echo "==> Codesign with identity: $SIGN_IDENTITY"
  # Sign nested frameworks/dylibs first (hardened runtime for notarization).
  find "$APP_PATH/Contents" -name "*.dylib" -o -name "*.framework" -type d 2>/dev/null | while read -r item; do
    codesign --force --options runtime --sign "$SIGN_IDENTITY" "$item" 2>/dev/null || true
  done
  if [[ -x "$MACOS_DIR/focusgaze-nm-host" ]]; then
    codesign --force --options runtime --sign "$SIGN_IDENTITY" "$MACOS_DIR/focusgaze-nm-host"
  fi
  codesign --force --deep --options runtime --sign "$SIGN_IDENTITY" "$APP_PATH"
  codesign --verify --verbose=2 "$APP_PATH"
else
  echo "==> Ad-hoc codesign (set SIGN_IDENTITY for Developer ID)"
  if [[ -x "$MACOS_DIR/focusgaze-nm-host" ]]; then
    codesign --force --sign - "$MACOS_DIR/focusgaze-nm-host" 2>/dev/null || true
  fi
  codesign --force --deep --sign - "$APP_PATH" 2>/dev/null || true
fi

mkdir -p "$DIST_DIR"
echo "==> Creating DMG at $DMG_PATH"
rm -f "$DMG_PATH"
STAGE="$DIST_DIR/dmg-stage"
rm -rf "$STAGE"
mkdir -p "$STAGE"
cp -R "$APP_PATH" "$STAGE/"
ln -sf /Applications "$STAGE/Applications"

# Lightweight README inside the DMG volume.
cat > "$STAGE/README.txt" <<EOF
focusGaze ${VERSION}
====================

1. Drag focusGaze.app to Applications.
2. Open focusGaze (right-click → Open on first launch if Gatekeeper warns).
3. Complete the first-run wizard: Camera tip → Chrome extension → Connect browser.
4. Turn Focus ON from the menu bar icon.

Chrome extension (dev): load unpacked from the repository extension/chrome/
Production: set FOCUSGAZE_EXTENSION_STORE_URL to the Chrome Web Store listing.

Optional: after first launch, the app registers a user-level Native Messaging host
(com.focusgaze.host) for Chrome / Edge / Brave / Chromium.

Signing / notarization: see scripts/sign_and_notarize.md
EOF

hdiutil create -volname "$APP_NAME" -srcfolder "$STAGE" -ov -format UDZO "$DMG_PATH"
rm -rf "$STAGE"

if [[ -n "${SIGN_IDENTITY:-}" ]]; then
  echo "==> Codesign DMG"
  codesign --force --sign "$SIGN_IDENTITY" "$DMG_PATH" || true
fi

if [[ -n "${APPLE_ID:-}" && -n "${APPLE_TEAM_ID:-}" && -n "${APPLE_APP_SPECIFIC_PASSWORD:-}" ]]; then
  echo "==> Notarize DMG (this may take several minutes)"
  xcrun notarytool submit "$DMG_PATH" \
    --apple-id "$APPLE_ID" \
    --team-id "$APPLE_TEAM_ID" \
    --password "$APPLE_APP_SPECIFIC_PASSWORD" \
    --wait
  xcrun stapler staple "$DMG_PATH" || true
  xcrun stapler staple "$APP_PATH" || true
  echo "==> Notarization complete"
elif [[ -n "${NOTARY_PROFILE:-}" ]]; then
  echo "==> Notarize DMG with keychain profile $NOTARY_PROFILE"
  xcrun notarytool submit "$DMG_PATH" --keychain-profile "$NOTARY_PROFILE" --wait
  xcrun stapler staple "$DMG_PATH" || true
else
  echo "==> Skipping notarization (set APPLE_ID + APPLE_TEAM_ID + APPLE_APP_SPECIFIC_PASSWORD,"
  echo "    or NOTARY_PROFILE from 'xcrun notarytool store-credentials')"
fi

echo ""
echo "Phase 5 artifacts:"
echo "  App: $APP_PATH"
echo "  NM host: $MACOS_DIR/focusgaze-nm-host"
echo "  DMG: $DMG_PATH"
echo "Done."
