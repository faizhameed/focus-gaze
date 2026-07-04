# focusGaze Bridge (Chrome MV3)

## Install & pair (no token paste)

1. Install this extension (Chrome Web Store in production, or **Load unpacked** → this folder for dev).
2. Run the focusGaze desktop app.
3. App tray → **Connect browser…** (or dashboard **Connect browser**).
4. A tab opens in **Google Chrome**; wait until it says **Connected**.

Fallback: tray → **Copy bridge token** and paste into this popup.

## Production

```bash
export FOCUSGAZE_EXTENSION_STORE_URL="https://chromewebstore.google.com/detail/<id>"
export FOCUSGAZE_CHROME_EXTENSION_ID="<web-store-extension-id>"  # if different from unpackaged key id
```
