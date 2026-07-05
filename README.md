# focusGaze

**Local productivity guardian** for macOS (Windows-oriented core). While Focus Mode is on, focusGaze watches browser tabs and (optionally) your camera for phone use, then raises **sticky alarms** until the distraction is cleared.

Everything runs **on your machine** by default: no account, no cloud sync, no network MITM.

---

## What it does

| Feature | Behavior |
|---------|----------|
| **Focus Mode** | Single on/off control (tray, dashboard, or Chrome extension) |
| **Browser monitoring** | Chrome extension reports active-tab URLs to a local HTTP bridge |
| **Blocklist alarms** | Social / time-waste sites raise a sticky alarm until those tabs are closed or navigated away |
| **Phone detection** | Optional camera + YOLO11n “cell phone” detection; cumulative in-use time in a rolling window can alarm |
| **Session stats** | Score and breakdown for on-task vs blocked vs phone time |
| **Privacy-first** | Local SQLite; no video retention by default; optional URL query redaction |

### Non-goals (v1)

- Cloud accounts or sync  
- System-wide proxy / HTTPS interception  
- Perfect ML on every phone model (thresholds are tunable)  
- App Store distribution (direct install / notarized build later)

---

## Architecture (short)

```
┌──────────────────────────────────────────────────────────┐
│  Qt 6 tray + dashboard (Focus, camera, status, stats)    │
└────────────────────────────┬─────────────────────────────┘
                             │
┌────────────────────────────▼─────────────────────────────┐
│  Core C++20 — sessions, URL classifier, alarms, stats,   │
│  phone window, SQLite (no Cocoa/Win32 in core)           │
└───────┬──────────────────┬──────────────────┬────────────┘
        │                  │                  │
   Camera / YOLO      HTTP bridge        Platform paths
   (OpenCV+ORT)     127.0.0.1:18765         macOS / Win
        │                  ▲
        │                  │
        │         Chrome MV3 extension
        │         (tabs → POST /v1/url)
```

Design mockups from Google Stitch live under [`design/`](design/README.md).  
Full product/architecture notes: [`IMPLEMENTATION.md`](IMPLEMENTATION.md).

---

## Requirements (macOS dev)

| Dependency | Role | Typical install |
|------------|------|-----------------|
| **CMake** ≥ 3.20 | Build | `brew install cmake` |
| **C++20 compiler** | clang via Xcode CLT | `xcode-select --install` |
| **SQLite3** | Persistence | system / `brew install sqlite` |
| **Qt 6 Widgets** | Tray + dashboard | `brew install qt` |
| **OpenCV** | Camera capture | `brew install opencv` |
| **ONNX Runtime** | YOLO11n inference | `brew install onnxruntime` |
| **Google Chrome** | Extension host | — |
| **Python 3** (optional) | Re-export YOLO model | — |

Paths in `CMakeLists.txt` assume Homebrew on Apple Silicon (`/opt/homebrew/...`). Adjust cache vars on Intel or custom prefixes.

---

## Quick start

### 1. Build

```bash
git clone <your-repo-url> focusgaze
cd focusgaze

cmake -S . -B build -DFOCUSGAZE_BUILD_GUI=ON -DFOCUSGAZE_BUILD_TESTS=ON
cmake --build build -j"$(sysctl -n hw.ncpu)"
```

Artifacts:

| Target | Path |
|--------|------|
| GUI app | `build/focusGaze.app` |
| CLI | `build/focusgaze` |
| Tests | `build/focusgaze_tests` |

### 2. Run the desktop app

```bash
open build/focusGaze.app
# or
./build/focusGaze.app/Contents/MacOS/focusGaze
```

A **menu-bar (tray) icon** appears. Useful items:

- **Turn Focus ON / OFF**
- **Open dashboard…** — Overview, camera device, Connect browser  
- **Show status…** / **Last session stats…** — in-app pages (not modal dialogs)  
- **Connect browser…** — one-time pair link for the extension  
- **Camera monitoring** — optional phone detection (pick device on Overview first)

Grant **Camera** access when prompted (`System Settings → Privacy & Security → Camera → focusGaze`).

### 3. Install the Chrome extension

**Development (unpacked):**

1. Chrome → `chrome://extensions`  
2. Enable **Developer mode**  
3. **Load unpacked** → select `extension/chrome/`

**Production:** publish to the Chrome Web Store, then set:

```bash
export FOCUSGAZE_EXTENSION_STORE_URL="https://chromewebstore.google.com/detail/<id>"
export FOCUSGAZE_CHROME_EXTENSION_ID="<store-extension-id>"  # if different from dev key id
```

Details: [`extension/chrome/README.md`](extension/chrome/README.md).

### 4. Pair extension ↔ app (no token paste)

1. App is running (bridge on `127.0.0.1`, default port **18765**).  
2. Tray or dashboard → **Connect browser…**  
3. Chrome opens a local pair page → wait until it says **Connected**.  

The app issues a **one-time code** (≈120s). The pair page sends the bridge token into the extension via `externally_connectable` / `chrome.runtime.sendMessage`.

**Fallback:** tray → **Copy bridge token** → paste into the extension popup → Save.

Pairing is **per Chrome profile** (each profile has its own extension storage).

---

## Day-to-day use

1. **Turn Focus ON** (tray, dashboard, or extension Focus toggle).  
2. Browse as usual — blocked sites (see blocklist below) raise a **sticky** alarm until fixed.  
3. Optionally enable **Camera monitoring** and choose the correct **camera device** (avoid Continuity/iPhone if you want the built-in webcam).  
4. **Turn Focus OFF** to end the session and update **Last session** stats.

If the selected camera is denied or produces no frames, monitoring is **turned off automatically** so the UI does not stay “enabled” falsely.

---

## Configuration

Settings and data (macOS default):

```text
~/Library/Application Support/focusGaze/
  settings.json
  blocklist.txt          # editable domain list
  focusgaze.db           # SQLite
  logs/
  models/                # optional YOLO override
```

| Setting | Meaning |
|---------|---------|
| `bridge_port` / `bridge_token` | Local extension auth (token auto-generated on first run) |
| `camera_monitoring_enabled` | Use camera for phone policy |
| `camera_device_index` | OpenCV/AVFoundation device index (`0`, `1`, …) |
| `phone_threshold_seconds` | Cumulative in-use seconds to alarm (dev default may be low for testing) |
| `phone_window_seconds` | Rolling window for phone accumulation |
| `privacy_redact` | Strip query strings when logging URLs |
| `resume_focus_on_launch` | Restore open Focus session after restart |
| `blocklist` / `allowlist` | Domains; allowlist wins over blocklist |

Template blocklist: [`resources/sample_blocklist.txt`](resources/sample_blocklist.txt).

### Useful environment variables

| Variable | Purpose |
|----------|---------|
| `FOCUSGAZE_DATA_DIR` | Override app data root |
| `FOCUSGAZE_YOLO_MODEL` | Path to `yolo11n.onnx` |
| `FOCUSGAZE_FAKE_CAMERA` | Video file path instead of live camera (tests/dev) |
| `FOCUSGAZE_QUIET` | Reduce console logging |
| `FOCUSGAZE_EXTENSION_STORE_URL` | “Get extension…” link |
| `FOCUSGAZE_CHROME_EXTENSION_ID` | Extension id for pair page messaging |
| `FOCUSGAZE_ROOT` | Repo root hint for tools that resolve paths |

---

## CLI (optional)

```bash
./build/focusgaze status
./build/focusgaze on | off | toggle
./build/focusgaze settings-show
./build/focusgaze stats
./build/focusgaze serve          # HTTP bridge + optional camera loop (no Qt tray)
```

Bridge endpoints (localhost only), when the GUI or `serve` is running:

| Method | Path | Notes |
|--------|------|--------|
| GET | `/v1/health` | Public liveness |
| GET | `/v1/status` | Auth: bearer / `X-FocusGaze-Token` |
| POST | `/v1/url` | Tab events from extension |
| POST | `/v1/focus` | `{ "on": true\|false }` |
| POST | `/v1/pair/start` | Mint one-time pair code |
| GET | `/v1/pair-ui?code=` | Pair page (open in **Chrome**) |
| GET | `/v1/install-help` | Short install + pair instructions |

---

## Tests

```bash
cmake --build build --target focusgaze_tests -j
cd build && ctest --output-on-failure
# or
./build/focusgaze_tests
```

Coverage includes sessions/focus desync, URL policy, sticky alarms, phone window, HTTP bridge + pairing, settings (including `camera_device_index`), stats, and camera helpers. GUI navigation is manual-smoke; core logic is unit-tested without requiring the tray.

---

## Project layout

```text
focusgaze/
├── src/
│   ├── core/           # Focus, storage, alarms, stats, settings (platform-agnostic)
│   ├── bridge/         # Localhost HTTP API for the extension
│   ├── vision/         # Camera, YOLO, preview helpers
│   ├── ui/             # Qt tray, dashboard, camera window
│   ├── adapters/       # macOS / Windows paths
│   └── app/            # CLI entry (focusgaze)
├── extension/chrome/   # MV3 bridge extension
├── models/             # yolo11n.onnx (+ export notes)
├── resources/          # Info.plist, sample blocklist
├── scripts/            # Model export, blocklist helpers
├── tests/              # Catch2 unit tests
├── design/             # Stitch UI references
├── IMPLEMENTATION.md   # Long-form product/architecture doc
└── CMakeLists.txt
```

---

## YOLO model

Phone vision uses **YOLO11n** ONNX (COCO class **67** = cell phone). See [`models/README.md`](models/README.md) for export and resolution order.

> Review Ultralytics / model license terms before redistributing binaries that ship weights.

---

## Privacy

- Bridge binds **`127.0.0.1` only**.  
- Pairing codes are **short-lived and single-use**.  
- Camera frames are processed locally for detection; they are **not** uploaded by the app.  
- URLs can be redacted via settings; your blocklist and DB stay on disk under Application Support.

---

## Development notes

- Prefer **platform-agnostic core** — OS details belong in `adapters/` and UI.  
- Project agent rules: [`AGENTS.md`](AGENTS.md) (docs on functions, tests with features, no drive-by commits unless asked).  
- Default branch for work: **`dev`**.

### CMake options

| Option | Default | Meaning |
|--------|---------|---------|
| `FOCUSGAZE_BUILD_GUI` | ON | Build Qt tray app |
| `FOCUSGAZE_BUILD_TESTS` | ON | Build Catch2 suite |

---

## Packaging (macOS DMG)

```bash
./scripts/package_macos_dmg.sh
# → dist/focusGaze-<version>.dmg  (ad-hoc sign by default)
```

For **Developer ID signing + notarization**, see [`scripts/sign_and_notarize.md`](scripts/sign_and_notarize.md).

Uses a separate `build-package/` tree so it does not overwrite the dev `build/` CMake options.

## First-run onboarding

On the first GUI launch (`onboarding_completed` is false in settings), a wizard walks through:

1. Welcome  
2. Camera privacy tip (+ open System Settings)  
3. Install Chrome extension  
4. **Connect browser** (auto-pair)

Skip is available; completing or skipping sets `onboarding_completed`.

## Troubleshooting

| Symptom | What to try |
|---------|-------------|
| Extension “offline” / bad token | App running? **Connect browser…** again, or re-copy token |
| Pair page fails outside Chrome | Pairing **must** open in **Google Chrome** (not Safari) |
| Wrong camera (iPhone Continuity) | Dashboard → **Camera device** → pick another index → re-enable monitoring |
| Camera monitoring won’t stay on | Permission denied or no frames — grant Camera access or change device |
| Tray “Last session” did nothing (old builds) | Use current build — opens in-app **Last session** page |
| Focus won’t turn off (old builds) | Update — OFF clears all open DB sessions and monitors |
| Build without YOLO/OpenCV | App still builds; phone vision disabled |

---

## License / attribution

- Application code: see repository license (add/clarify as you publish).  
- Third parties: nlohmann/json, cpp-httplib, Catch2, OpenCV, ONNX Runtime, Qt, Ultralytics YOLO — their licenses apply.

---

## Roadmap (high level)

- [ ] Chrome Web Store listing + polished onboarding  
- [ ] Signed / notarized macOS distribution  
- [ ] Windows adapters parity  
- [ ] Stronger native notifications / alarm overlay  
- [ ] Optional Native Messaging host as alternate pairing path  

Contributions and feedback welcome once the public remote is set up.
