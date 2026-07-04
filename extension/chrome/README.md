# focusGaze Bridge (Chrome MV3)

## Pair with the desktop app (no token paste)

1. Install this extension (Web Store in production, or **Load unpacked** for dev).
2. Run the focusGaze desktop app.
3. In the app: tray → **Connect browser (auto pair)…** (or dashboard **Connect browser**).
4. A tab opens in **Google Chrome** on `http://127.0.0.1:<port>/v1/pair-ui?code=…`.
5. When it says **Connected**, the bridge token is stored in the extension automatically.

How it works:

- App mints a **one-time code** (120s, single use).
- Pair page calls `GET /v1/pair/session?code=…` then  
  `chrome.runtime.sendMessage(extensionId, { type: "focusgaze.pair", token, port })`.
- Service worker handles `onMessageExternal` (see `externally_connectable` in `manifest.json`).

Fallback: tray → **Copy bridge token** and paste into the popup.

## Production Web Store

Set the app env var when published:

```bash
export FOCUSGAZE_EXTENSION_STORE_URL="https://chromewebstore.google.com/detail/<your-id>"
# After publish, if the store ID differs from the unpacked key id:
export FOCUSGAZE_CHROME_EXTENSION_ID="<web-store-extension-id>"
```

## Dev: all profiles

```bash
./scripts/install_chrome_extension.sh
```

See `scripts/chrome_extension_installer.py` (unpacked + developer mode).
