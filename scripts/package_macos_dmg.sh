#!/usr/bin/env bash
# Build a distributable focusGaze.app and optional DMG for macOS.
# Signing/notarization run only when identities/credentials are provided.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# Use a dedicated package build dir so we don't clobber the dev `build/` CMake cache.
BUILD_DIR="${BUILD_DIR:-$ROOT/build-package}"
DIST_DIR="${DIST_DIR:-$ROOT/dist}"
APP_NAME="focusGaze"
APP_PATH="$BUILD_DIR/${APP_NAME}.app"
VERSION="$(grep -E 'project\(focusGaze VERSION' "$ROOT/CMakeLists.txt" | sed -E 's/.*VERSION ([0-9.]+).*/\1/')"
DMG_PATH="$DIST_DIR/${APP_NAME}-${VERSION}.dmg"

# Optional: export SIGN_IDENTITY="Developer ID Application: Your Name (TEAMID)"
# Optional notarization: APPLE_ID, APPLE_TEAM_ID, APPLE_APP_SPECIFIC_PASSWORD (or keychain profile)

echo "==> Configuring & building GUI bundle (BUILD_DIR=$BUILD_DIR)"
cmake -S "$ROOT" -B "$BUILD_DIR" -DFOCUSGAZE_BUILD_GUI=ON -DFOCUSGAZE_BUILD_TESTS=OFF
cmake --build "$BUILD_DIR" --target focusGaze_gui -j"$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"

if [[ ! -d "$APP_PATH" ]]; then
  echo "error: expected app at $APP_PATH" >&2
  exit 1
fi

mkdir -p "$DIST_DIR"

if [[ -n "${SIGN_IDENTITY:-}" ]]; then
  echo "==> Codesign with identity: $SIGN_IDENTITY"
  codesign --force --deep --options runtime --sign "$SIGN_IDENTITY" "$APP_PATH"
  codesign --verify --verbose=2 "$APP_PATH"
else
  echo "==> Skipping codesign (set SIGN_IDENTITY to enable Developer ID signing)"
  # Ad-hoc sign so the app is at least consistent for local Gatekeeper experiments.
  codesign --force --deep --sign - "$APP_PATH" 2>/dev/null || true
fi

echo "==> Creating DMG at $DMG_PATH"
rm -f "$DMG_PATH"
STAGE="$DIST_DIR/dmg-stage"
rm -rf "$STAGE"
mkdir -p "$STAGE"
cp -R "$APP_PATH" "$STAGE/"
ln -sf /Applications "$STAGE/Applications"

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
echo "Artifacts:"
echo "  App: $APP_PATH"
echo "  DMG: $DMG_PATH"
echo "Done."
