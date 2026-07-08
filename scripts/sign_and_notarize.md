# macOS signing & notarization

`scripts/package_macos_dmg.sh` builds `focusGaze.app`, packs a DMG, and optionally signs/notarizes.

## Prerequisites

1. Apple Developer Program membership  
2. **Developer ID Application** certificate in Keychain  
3. App-specific password or notarytool keychain profile  

## Unsigned / local DMG (CI or personal)

```bash
./scripts/package_macos_dmg.sh
# → dist/focusGaze-<version>.dmg (ad-hoc signed app)
```

## Signed + notarized release

```bash
export SIGN_IDENTITY="Developer ID Application: Your Name (TEAMID)"
export APPLE_ID="you@example.com"
export APPLE_TEAM_ID="TEAMID"
export APPLE_APP_SPECIFIC_PASSWORD="xxxx-xxxx-xxxx-xxxx"
./scripts/package_macos_dmg.sh
```

Or store credentials once:

```bash
xcrun notarytool store-credentials "focusgaze-notary" \
  --apple-id "you@example.com" --team-id "TEAMID" --password "app-specific-password"
export SIGN_IDENTITY="Developer ID Application: Your Name (TEAMID)"
export NOTARY_PROFILE="focusgaze-notary"
./scripts/package_macos_dmg.sh
```

## Verify

```bash
spctl -a -vv dist/focusGaze-*.dmg
codesign -dv --verbose=4 build/focusGaze.app
```
