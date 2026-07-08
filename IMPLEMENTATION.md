# focusGaze — Implementation Document

**Product name:** focusGaze  
**Type:** Local desktop productivity guardian (C++20)  
**Primary platform:** macOS (installable)  
**Secondary platform:** Windows (same core, OS adapters)  
**Status:** Mac product through **Phase 5** (installable `.app` / DMG, onboarding, Native Messaging host, focus segments). Phase 6 (Windows) not started.

---

## 1. Product summary

focusGaze helps the user stay productive by combining **browser URL awareness**, **camera-based distraction detection**, and **sticky alarms** while Focus Mode is on. All processing is local by default (no cloud required for v1).

### Features

| # | Feature | Behavior |
|---|---------|----------|
| 1 | Focus Mode ON | Single control (UI + tray) enables monitoring and policy enforcement |
| 2 | Camera watch | Continuous low-FPS capture while Focus Mode is on |
| 3 | Browser URL log | Record navigations / active tab URLs from the browser |
| 4 | Social / time-waste alarm | Alarm on blocked sites; **does not stop until offending tab(s) are closed** (or navigated off blocked domains) |
| 5 | Phone distraction rule | If user holds/appears with phone **more than 1 minute cumulative** inside a **rolling 30-minute window**, raise alarm |
| 6 | Productivity statistics | Session and daily breakdown of on-task vs off-task time, phone incidents, alarms |

### Non-goals (v1)

- Cloud sync or accounts
- Full network MITM / system-wide proxy
- Guaranteed ML accuracy on every phone model (tunable thresholds instead)
- App Store distribution (optional later; start with signed/notarized direct download)

---

## 2. Goals and constraints

### Goals

1. Ship an **installable Mac app** (`.app` / DMG or `.pkg`).
2. Keep **business logic platform-agnostic** so Windows is adapter work, not a rewrite.
3. Prefer **privacy**: no video retention by default; optional URL query redaction; local SQLite.
4. Make core policies **unit-testable** without camera or browser.

### Constraints

- macOS has **no public API** for “all browser URLs”; use a **browser extension + local bridge** as primary.
- Camera and notifications require **TCC permissions**; debug binary vs installed `.app` may be treated as different apps.
- Alarms must remain effective under **mute / Do Not Disturb** via always-on-top visual overlay (sound secondary).

---

## 3. Architecture

```
┌─────────────────────────────────────────────────────────┐
│  UI Layer (Qt 6 — tray, Focus toggle, stats, settings)  │
└───────────────────────┬─────────────────────────────────┘
                        │
┌───────────────────────▼─────────────────────────────────┐
│  Core (pure C++20 — no Cocoa / Win32)                   │
│  • FocusSession / SessionStore                          │
│  • UrlClassifier (social / work / neutral)              │
│  • DistractionPolicy                                    │
│  • PhonePresenceTracker (1 min in 30 min window)        │
│  • ProductivityStats / Reports                          │
│  • AlarmController (sticky until condition clears)      │
│  • Storage (SQLite)                                     │
└───────┬─────────────┬─────────────┬─────────────────────┘
        │             │             │
   ┌────▼────┐  ┌─────▼─────┐  ┌───▼──────────┐
   │ Camera  │  │ Browser   │  │ Notification │
   │ Adapter │  │ URL Source│  │ / Audio      │
   └────┬────┘  └─────┬─────┘  └───┬──────────┘
   macOS AVF /    Extension +     macOS notif
   OpenCV         localhost HTTP  + sound
   Windows MF     (same on Win)   Windows toast
```

**Rule:** Core never calls OS APIs directly. Only **adapters** and **UI** do. That is what makes Windows portability feasible.

### Component responsibilities

| Component | Responsibility |
|-----------|----------------|
| `FocusSession` | Lifecycle of a focus period; owns active policies |
| `UrlClassifier` | Map URL → category (blocked social, allowlisted work, neutral) |
| `BrowserUrlSource` | Interface for URL events (`activated`, `updated`, `closed`) |
| `HttpBrowserBridge` | Local HTTP server implementing `BrowserUrlSource` for extensions |
| `PhonePresenceTracker` | Rolling-window accumulation of phone-visible seconds |
| `AlarmController` | Raise/clear alarms; multi-reason stacking (social + phone) |
| `ProductivityStats` | Aggregate session/daily metrics and “went off” summary |
| `Storage` | SQLite persistence |
| `VisionPipeline` | Frames → phone/person signals (OpenCV / ONNX) |
| `PlatformPaths` | App support dirs, logs, config per OS |
| `UiShell` | Qt main window, tray, overlay, settings, charts |

---

## 4. Recommended technology stack

| Layer | Choice | Rationale |
|--------|--------|-----------|
| Language | C++20 | Product requirement |
| Build | CMake + `CMakePresets.json` | One project for macOS and Windows |
| UI | Qt 6 (Widgets or QML) | Tray, settings, overlays, charts; strong Mac/Windows support |
| Vision | OpenCV + optional ONNX Runtime | Portable phone detection |
| Camera | Qt Multimedia and/or OpenCV `VideoCapture` | Fast path on both OS families |
| Storage | SQLite (`sqlite3` or SQLiteCpp) | URL log, sessions, stats |
| Config | JSON (`nlohmann/json`) | Human-editable settings |
| Logging | spdlog | Console + rotating file |
| HTTP bridge | cpp-httplib or Qt `QTcpServer` | Extension ↔ app |
| Mac package | `MACOSX_BUNDLE` → create-dmg / `productbuild` | Installable artifact |
| Win package (later) | MSVC build → WiX or Inno Setup | Same CMake targets |
| CI | GitHub Actions `macos-latest`, later `windows-latest` | Catch portability early |
| Unit tests | Catch2 or GoogleTest + CTest | Core policies without hardware |

**UI alternative:** Dear ImGui + native tray (thinner binary). Qt remains the default recommendation for installable desktop UX.

---

## 5. Data model (SQLite)

Suggested schema (evolve with migrations if needed):

```sql
-- Focus periods
CREATE TABLE sessions (
  id            INTEGER PRIMARY KEY,
  started_at    INTEGER NOT NULL,  -- unix epoch seconds
  ended_at      INTEGER,
  focus_enabled INTEGER NOT NULL DEFAULT 1
);

-- Browser events (append-oriented)
CREATE TABLE url_events (
  id            INTEGER PRIMARY KEY,
  session_id    INTEGER REFERENCES sessions(id),
  ts            INTEGER NOT NULL,
  url           TEXT NOT NULL,
  domain        TEXT,
  title         TEXT,
  tab_id        TEXT,
  browser       TEXT,
  event         TEXT NOT NULL,     -- activated | updated | closed
  category      TEXT               -- blocked | allow | neutral
);

-- Phone visibility intervals (derived from vision)
CREATE TABLE phone_events (
  id            INTEGER PRIMARY KEY,
  session_id    INTEGER REFERENCES sessions(id),
  started_at    INTEGER NOT NULL,
  ended_at      INTEGER,
  confidence    REAL
);

-- Alarm history
CREATE TABLE alarms (
  id            INTEGER PRIMARY KEY,
  session_id    INTEGER REFERENCES sessions(id),
  reason        TEXT NOT NULL,     -- social_tab | phone_window
  raised_at     INTEGER NOT NULL,
  cleared_at    INTEGER,
  meta_json     TEXT               -- e.g. tab ids, domains
);

-- Optional pre-aggregated daily rollup
CREATE TABLE daily_stats (
  day           TEXT PRIMARY KEY,  -- YYYY-MM-DD local
  focus_seconds INTEGER,
  productive_seconds INTEGER,
  social_seconds INTEGER,
  phone_seconds INTEGER,
  alarm_count   INTEGER
);
```

### On-disk locations

| OS | Path |
|----|------|
| macOS | `~/Library/Application Support/focusGaze/` |
| Windows | `%APPDATA%\focusGaze\` |

Override for tests: `FOCUSGAZE_DATA_DIR`.

---

## 6. Policy specifications

### 6.1 Focus Mode

- **ON:** Start (or resume) session; accept URL events; run camera pipeline; enforce alarms; record metrics.
- **OFF:** Stop enforcing alarms (clear active alarms or suppress — **default: clear and stop**); stop camera; finalize session stats.
- Persist last toggle state; optional “resume Focus on launch” setting (default off for safety).

### 6.2 URL classification

- Load **blocklist** (defaults: major social / time-waste domains) and **allowlist** (user work sites).
- Allowlist wins over blocklist.
- Optional **privacy mode:** store domain only or strip query strings.
- “Typed URL” vs navigation: treat **committed navigations / active tab URLs** from the extension as source of truth (browser-dependent).

Default blocklist examples (configurable): `instagram.com`, `tiktok.com`, `x.com`, `twitter.com`, `reddit.com`, `facebook.com`, `netflix.com`, etc. YouTube may be **neutral** or blocked by user preference.

### 6.3 Social sticky alarm

1. While Focus ON, if any open tab’s URL classifies as **blocked**, raise `social_tab` alarm.
2. Alarm remains active until **no blocked tabs remain** (all closed or navigated to non-blocked domains).
3. **No snooze** in default policy (matches product intent).
4. Multi-tab: one social tab is enough to keep alarm on.
5. Focus OFF clears/suppresses alarm.

Presentation: looping sound **and** always-on-top overlay (“Close social tab(s) to continue”). Do not rely only on notification banners.

### 6.4 Phone rule (1 minute / 30 minutes)

1. Vision emits phone-visible intervals (debounced: e.g. require majority of recent frames).
2. `PhonePresenceTracker` maintains **cumulative phone-visible seconds** in a **rolling 30-minute** window.
3. If cumulative **> 60 seconds** while Focus ON → raise `phone_window` alarm.
4. **Clear policy (recommended default A):** alarm clears when phone is not detected for a short cooldown (e.g. 3–5 seconds continuous absence).
5. Alternative **policy B** (optional setting): fixed alarm duration + log violation only.

False-positive mitigation: multi-frame consensus, confidence threshold, user sensitivity slider in settings.

### 6.5 Productivity statistics

Per session and daily:

- Focus duration (ON time)
- Time on productive / allowlisted domains
- Time on social / blocked domains
- Phone distraction minutes and incident count
- Alarm count and mean time-to-clear

**“How productive when you went off”** = last session summary card + simple score, e.g.:

```text
productive_ratio = productive_time / max(focus_time, 1)
score = productive_ratio
        - penalty(social_minutes)
        - penalty(phone_incidents)
```

Exact weights are settings-tunable; document defaults in UI.

---

## 7. Browser integration (critical path)

### Primary: extension + localhost API

1. focusGaze runs a **local HTTP server** on `127.0.0.1` (configurable port) with a **shared secret token**.
2. Chrome/Edge MV3 extension (Firefox optional) posts events on tab activate / update / remove.
3. Same extension approach works on **Windows** with zero OS-specific URL code in core.

Suggested event payload:

```json
{
  "url": "https://www.instagram.com/",
  "title": "Instagram",
  "tabId": "123",
  "browser": "chrome",
  "event": "activated",
  "ts": 1710000000
}
```

### Fallback (Mac-only, fragile)

- Accessibility / AppleScript polling of frontmost browser URL — optional enhancement for window title only; **not** primary for v1.

### Avoid for v1

- System-wide MITM proxy (privacy and trust cost).

### Why the extension remains long-term

- Sticky “alarm until tab closes” needs **tab identity + URL**, which the OS and passive network inspection (TLS SNI / DNS) cannot provide reliably for HTTPS.
- Network-based detection may be added later as an **optional backup** (other apps, missing extension), not as the primary policy engine.
- **Dev friction** (load unpacked, paste token, per Chrome profile) is **not** the final product UX; distribution and pairing are polished in packaging phases (below). Manual token entry every run is explicitly **temporary**.

---

## 8. Browser extension distribution and install UX

Chrome (and Chromium browsers) **do not allow a normal desktop installer to silently force-install an extension** for consumer users—the same way malware would. A supported product still **installs the app automatically** and gets the extension through **Chrome’s approved channels**, with **user acceptance** where Chrome requires it (or **org policy** on managed devices).

### Goals after a full install

1. focusGaze app installed (e.g. `/Applications`, optional login item / tray).
2. Extension present in the user’s **primary** Chrome (or Edge) profile.
3. **Paired once** to the local app so day-to-day use does not require re-pasting a token.
4. Clear path if the user uses **additional Chrome profiles** (extensions are per-profile by Chrome’s design).

### What Chrome allows

| Approach | “Automatic” on app install? | User involvement | Use for focusGaze |
|----------|----------------------------|------------------|-------------------|
| **Chrome Web Store** (or Edge Add-ons) + “Add extension” / first-run button | No full silent install | User clicks **Add to Chrome** (or equivalent) once | **Default for consumers** |
| **Enterprise policy** (`ExtensionInstallForcelist` via MDM / plist / Group Policy) | Yes, forced by org | Admin/IT configures policy; user may not see Store prompt | **Managed / enterprise SKU** |
| **Load unpacked / sideload `.crx` from installer** | Partial / fragile | Developer mode; Chrome blocks casual sideload for security | **Dev / Phase 2 only** (current workflow) |
| **Native Messaging host** installed with the app | Host yes; extension still needed | Little after extension is installed | **GA pairing path** (optional alongside or instead of long-lived Bearer token in UI) |
| **Post-install open Store / setup URL** | Semi | One click from wizard | **Recommended GA onboarding** |

### Recommended rollout

#### Phase A — First-run wizard (ship with Mac UI / packaging)

On first launch of the installed app (before or as part of Phase 5):

1. Screen: **Connect your browser**.
2. Primary CTA: **Install Chrome extension** → opens the Chrome Web Store listing (or Edge Add-ons).
3. User completes **Add to Chrome** (required acceptance for consumer installs).
4. **Automatic pairing**: extension obtains a stable credential (see Pairing) and shows **Connected**; app shows the same.
5. If Chrome is not running, offer **Open Chrome and finish setup**.
6. Permissions already planned: Camera, Notifications; plus guidance for extension.

Manual “paste `bridge_token` into extension popup” remains acceptable for **local CLI / unpacked extension** development only.

#### Phase B — Installer also “installs extension” (product meaning)

**Mac (DMG / `.pkg`):**

- Installs `focusGaze.app`.
- Optionally installs a **Chrome Native Messaging** host manifest under the user or system NativeMessagingHosts path (and Chromium/Edge variants if supported).
- **Does not** silently inject the extension into Chrome profiles for normal users.
- Offers checkbox or post-install step: **Install browser extension now** → Store link / deep link; user accepts in Chrome.

**Windows (MSI / Inno Setup / WiX):**

- Same pattern: app + optional native messaging registration.
- Extension via Store link or post-install launch of Chrome to the listing.
- Enterprise docs: Group Policy / MDM **force-install** by extension ID.

#### Phase C — Managed / truly automatic (optional SKU)

For schools and companies:

- Document Chrome enterprise policies to **force-install** the extension by ID + update URL (Web Store).
- Deploy via **MDM** (e.g. Jamf, Intune) or Windows Group Policy.
- Acceptance is at **organization** policy level; end users may not click Add to Chrome.

This is the only widely supported **fully automatic** extension install path.

### Pairing (persistent; not “token every run”)

| Mechanism | User experience | Persistence |
|-----------|-----------------|-------------|
| **One-time pair from wizard** | “Connect” in extension after Store install | Token/credential in app data dir + extension `storage` (local or sync per profile) |
| **Native Messaging** | Minimal UI after grant | Extension talks to local host; popup token field unnecessary |
| **Short-lived pair code** | Enter one code on first connect | Then same as long-lived credential |

**Product rule:** After GA onboarding, users must **not** need to re-enter a token on every app launch. Regenerating `bridge_token` only when the user resets pairing or wipes app data.

### Multiple Chrome profiles

- Chrome isolates extensions **per profile**. No consumer installer can reliably install into every profile on a machine without enterprise policy.
- **Primary path:** complete setup for the **default / everyday** profile in the first-run wizard.
- **Secondary path:** in-app or extension copy: “Not seeing site alerts? Enable focusGaze in this Chrome profile” → Store or re-open pairing.
- Optional later: if the app is running, Focus is ON, Chrome is frontmost, and **no URL events** arrive for N minutes → gentle prompt to connect the extension in the active profile.

Do **not** promise “one install covers all Chrome profiles forever” for consumer Chrome.

### Other browsers (distribution note)

| Browser | Plan |
|---------|------|
| **Chrome / Brave / Chromium** | MV3 extension; Chrome Web Store (or policy). |
| **Edge** | Same extension family where possible; Edge Add-ons listing. |
| **Firefox** | Separate add-on + AMO; same pairing concepts. |
| **Safari** | **Safari Web Extension / App Extension** packaged with the Mac app (Xcode); user enables in Safari settings—different from Chrome Store. Plan as a **later Mac phase** if needed. |

v1 target remains **Chrome-first** (current `extension/chrome/`); Edge is a low-cost follow-on; Safari is a separate packaging track.

### Security rationale (document for stakeholders)

Silent extension install from an arbitrary desktop installer would be abused by malware. Chrome requires Store review and/or explicit user action, or enterprise policy. Product copy should treat **“User clicks Add to Chrome / Accept once”** as **correct and expected**, not a defect.

### What stays vs what changes after full product build

| Layer | Stays long-term | Changes from current Phase 2 CLI |
|--------|-----------------|-----------------------------------|
| Extension (or Safari app extension) as URL/tab source | **Yes** | Better install + one-time pairing |
| Local bridge (HTTP and/or Native Messaging) | **Yes** | Hide secrets from daily UX; auto-pair |
| Manual token every run / every profile setup | **No** as GA UX | Stable credential after first-run |
| File-based `blocklist.txt` (+ sample template in repo) | **Yes** | Also editable in settings UI later |
| CLI `serve` | Useful for dev/ops | Main path = tray app runs bridge when Focus is on |
| Installer “also installs extension” | **Yes, via Store + accept or MDM policy** | Not via silent profile injection for consumers |

### Alignment with phases

| When | Extension distribution work |
|------|----------------------------|
| **Phase 2 (current)** | Unpacked extension + Bearer token; document limitations. |
| **Phase 5 (Mac installable)** | First-run wizard: Store link, pairing, permissions; optional Native Messaging host in `.app` / pkg. |
| **Phase 6 (Windows)** | Same onboarding; installer registers host; Store/Add-ons link. |
| **Post-GA / enterprise** | Policy docs for force-install; optional Safari extension. |

### Non-goals for extension install

- Silently installing into all Chrome profiles without user or admin consent.
- Depending on permanent sideload / Developer mode for GA users.
- Replacing the extension entirely with network MITM for sticky tab alarms in v1.

---

## 9. Camera and vision pipeline

1. Capture at **2–5 FPS** while Focus ON (configurable).
2. Background thread: frame → optional person presence → phone-like object detection (OpenCV DNN / ONNX).
3. Emit high-level events only (`PhoneVisible`, `UserAway`); **do not store video** by default.
4. Optional debug preview in settings (off by default).
5. Pause or throttle when lid closed / no user / Focus OFF.

**Test affordance:** `FOCUSGAZE_FAKE_CAMERA=/path/to/video` to replay frames deterministically.

---

## 10. Platform permissions

### macOS

| Capability | Mechanism | Notes |
|------------|-----------|--------|
| Camera | `NSCameraUsageDescription` in Info.plist | TCC prompt |
| Notifications | User notification authorization | Secondary to overlay |
| Accessibility | Only if using optional UI scripting | Optional |
| Auto-start | Login Item / Launch Agent | Opt-in |

Bundle ID example: `com.yourorg.focusgaze`.

### Windows (later)

| Capability | Mechanism |
|------------|-----------|
| Camera | OpenCV / Media Foundation |
| Notifications | Toast notifications |
| Install | MSI/EXE |

---

## 11. Repository layout

```text
focusGaze/
├── IMPLEMENTATION.md          # this document
├── CMakeLists.txt
├── CMakePresets.json
├── README.md                  # user-facing; add when scaffolding
├── resources/                 # icons, alarm sounds, Info.plist.in
├── extension/
│   ├── chrome/                # MV3 manifest + background
│   └── shared/                # shared JS if multi-browser
├── src/
│   ├── app/main.cpp
│   ├── core/                  # no OS includes
│   │   ├── FocusSession.hpp / .cpp
│   │   ├── UrlClassifier.hpp / .cpp
│   │   ├── PhonePresenceTracker.hpp / .cpp
│   │   ├── AlarmController.hpp / .cpp
│   │   ├── ProductivityStats.hpp / .cpp
│   │   └── Storage.hpp / .cpp
│   ├── adapters/
│   │   ├── macos/
│   │   └── windows/           # stubs early
│   ├── vision/
│   ├── bridge/                # local HTTP for extension
│   └── ui/                    # Qt
├── testdata/
│   └── vision/                # sample videos for fake camera
├── scripts/
│   └── smoke_mac.sh           # build + ctest + curl scenarios
└── tests/                     # core unit tests
```

---

## 12. Phased implementation plan

### Phase 0 — Project skeleton (1–2 days)

1. CMake project with `src/core`, `adapters/macos`, `adapters/windows` (stub), `src/ui`, `src/app`.
2. C++20, sensible warnings; fetch Qt6, OpenCV, SQLite, nlohmann/json, spdlog.
3. App entry + system tray + Focus ON/OFF wired to in-memory `FocusSession`.
4. macOS Info.plist keys (camera, notifications), bundle metadata.
5. Console logging for session start/stop.

**Exit criteria:** App launches on Mac; Focus toggle works and logs.

### Phase 1 — Persistence and config (1–2 days)

1. SQLite schema and `Storage` API.
2. `PlatformPaths` (Mac implementation; Win stub).
3. Settings: blocklist, allowlist, phone threshold (60s), window (30m), alarm sound, privacy redact.
4. Session start/stop persists to DB.

**Exit criteria:** Toggle creates session rows; settings load/save under Application Support (or `FOCUSGAZE_DATA_DIR`).

### Phase 2 — Browser URL pipeline (3–5 days) — critical path

1. Local control channel on `127.0.0.1` with bearer token.
2. Chrome MV3 extension → POST events to app.
3. `BrowserUrlSource` + `HttpBrowserBridge`.
4. `UrlClassifier` + default blocklist.
5. Sticky social alarm (sound + overlay).
6. Clear when extension reports tab closed / domain no longer blocked.

**Exit criteria:** Instagram (or equivalent) with Focus ON alarms; closing tab stops alarm; URLs in SQLite.

### Phase 3 — Camera and phone detection (4–7 days)

1. Camera capture module.
2. Optional settings preview (debug).
3. ONNX/OpenCV DNN or strong heuristic for phone presence.
4. `PhonePresenceTracker` rolling window.
5. Alarm on threshold; clear per policy A.
6. Performance: throttle FPS; inference off UI thread.

**Exit criteria:** ~1+ minute phone-visible cumulative in 30m triggers alarm; putting phone down clears (policy A).

### Phase 4 — Statistics UI (2–3 days)

1. Aggregation queries for session end and daily dashboard.
2. Timeline / breakdown UI (productive, social, phone, idle).
3. Score and “last focus session” card after Focus OFF.
4. Optional CSV/JSON export.

**Exit criteria:** After a session, user sees an accurate productivity breakdown.

### Phase 5 — Mac installable product (2–3 days)

1. `MACOSX_BUNDLE`, icon, versioning.
2. Code signing (Developer ID) and notarization for distribution.
3. DMG or `.pkg`; first-run wizard (permissions + **Connect browser** / Web Store link + pairing — see **§8**).
4. Optional Native Messaging host install alongside the app.
5. Optional Sparkle auto-update (deferrable).

**Exit criteria:** Clean Mac install; Focus Mode works after permissions + extension accepted in Chrome + one-time pairing (no daily token paste).

### Phase 6 — Windows portability (3–5 days, after Mac is solid)

1. Windows `PlatformPaths`, notifications, audio.
2. Same Qt UI; MSVC CMake preset.
3. Reuse extension + localhost URL path unchanged.
4. Camera via OpenCV on Windows.
5. Inno Setup / WiX installer.
6. CI matrix builds both OS.

**Exit criteria:** Feature parity for Focus, URLs, alarms, camera policy, stats.

---

## 13. MVP vs later

| Tier | Scope |
|------|--------|
| **MVP (ship Mac first)** | Focus ON/OFF; extension URL log; social sticky alarm; basic session stats; installable `.app`/DMG (unsigned OK for personal) |
| **v1.1** | Camera + phone 1-min / 30-min rule |
| **v1.2** | Rich dashboard, allowlists UX, multi-browser polish |
| **v2** | Windows adapters + installer |

Rationale: URL enforcement is more deterministic early than vision; validate UX and alarm policies before investing heavily in ML.

---

## 14. Environment variables (dev and test)

| Variable | Purpose |
|----------|---------|
| `FOCUSGAZE_DATA_DIR` | Override SQLite/config root (isolate test state) |
| `FOCUSGAZE_PORT` | Fixed local bridge port (e.g. `18765`) |
| `FOCUSGAZE_FAKE_CAMERA` | Path to video file instead of webcam |
| `FOCUSGAZE_FAKE_URLS` | Enable scripted URL injection mode if implemented |
| `FOCUSGAZE_ALARM_DRY_RUN` | Log alarms; optional quieter/no sound |
| `FOCUSGAZE_TIME_SCALE` | Speed up policy clocks in tests (e.g. `10` = 10×) |

Example reset:

```bash
rm -rf /tmp/fg-test && mkdir -p /tmp/fg-test
FOCUSGAZE_DATA_DIR=/tmp/fg-test FOCUSGAZE_PORT=18765 ./build/bin/focusGaze
```

---

## 15. Local testing on macOS

### 14.1 Dev run loop (prefer build tree over installer)

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH="$(brew --prefix qt@6)"
cmake --build build -j
./build/focusGaze.app/Contents/MacOS/focusGaze   # if MACOSX_BUNDLE
# or
./build/bin/focusGaze
```

Attach `lldb` as needed. Use rotating logs under the data dir.

### 14.2 Test pyramid

```text
        ┌──────────────────┐
        │ Manual (camera,  │  weekly / pre-release
        │ install, TCC)    │
        ├──────────────────┤
        │ Integration      │  extension API + alarm + DB
        │ (app process)    │
        ├──────────────────┤
        │ Unit (core)      │  classifier, tracker, stats
        └──────────────────┘  every commit, no camera
```

### 14.3 Unit tests (every commit)

```bash
cmake --build build --target focusgaze_tests
ctest --test-dir build --output-on-failure
```

**Required cases:**

- `UrlClassifier`: blocked domains, allowlist precedence, subdomains
- `PhonePresenceTracker`: rolling 30m window; threshold at 60s; expiry of old intervals
- `AlarmController`: sticky social until all bad tabs clear; multi-tab; Focus OFF clears
- `ProductivityStats`: ratios, empty session, no cross-session bleed

### 14.4 Integration without browser (curl / script)

Inject the same HTTP API the extension uses:

```bash
TOKEN=dev-token
curl -s -X POST "http://127.0.0.1:18765/v1/url" \
  -H "Authorization: Bearer $TOKEN" \
  -H "Content-Type: application/json" \
  -d '{"url":"https://www.instagram.com/","tabId":"t1","event":"activated","ts":'$(date +%s)'}'

curl -s -X POST "http://127.0.0.1:18765/v1/url" \
  -H "Authorization: Bearer $TOKEN" \
  -H "Content-Type: application/json" \
  -d '{"tabId":"t1","event":"closed","ts":'$(date +%s)'}'
```

Assert alarm state via status endpoint and/or SQLite.

### 14.5 Browser acceptance (Chrome unpacked)

1. `chrome://extensions` → Developer mode → Load unpacked → `extension/chrome`
2. Prefer a **second Chrome profile** so daily browsing is unaffected
3. Point extension at app port + token
4. Manual matrix:

| Steps | Expected |
|-------|----------|
| Open `example.com` | Logged in DB |
| Open blocked social site, Focus ON | Alarm on |
| Switch away but leave social tab open | Alarm stays |
| Close social tab(s) | Alarm stops |
| Navigate social tab to safe domain | Alarm stops |
| Focus OFF, open social | No alarm (default) |
| Allowlisted domain that would otherwise block | No alarm; still logged |

### 14.6 Camera / phone testing

| Tier | Method |
|------|--------|
| 1 (best) | `FOCUSGAZE_FAKE_CAMERA` + sample videos; optional `FOCUSGAZE_TIME_SCALE` |
| 2 | Live webcam hold phone / put down |
| 3 | Inject synthetic intervals into `PhonePresenceTracker` (no OpenCV) |

Live checks: no false alarm on face-only; trip after cumulative ≥ 60s; clear when phone leaves frame (policy A).

**TCC note:** Terminal-launched binary and installed `.app` may need **separate** camera grants. Re-test permissions on the **shipped** bundle.

### 14.7 Stats verification

```bash
sqlite3 /tmp/fg-test/focusgaze.db "SELECT id, started_at, ended_at FROM sessions ORDER BY id DESC LIMIT 3;"
sqlite3 /tmp/fg-test/focusgaze.db "
  SELECT domain, COUNT(*) FROM url_events GROUP BY domain ORDER BY 2 DESC LIMIT 10;
"
```

Cross-check UI session summary against DB aggregates.

### 14.8 Alarm / UX checks

- Overlay visible under Do Not Disturb and system mute
- Multi-display behavior defined and verified
- Sleep/wake does not leave inconsistent alarm vs DB
- Debug builds use clearly audible default sound for failures

### 14.9 Packaging checks (Phase 5)

- Run Release `.app` from `/Applications`
- DMG drag-install path
- Gatekeeper / quarantine simulation (`spctl -a -vv …`)
- Fresh user / VM walkthrough: camera, notifications, extension, one full social + phone scenario

### 14.10 Day-to-day workflow

**Every commit:**

```bash
cmake --build build -j && ctest --test-dir build --output-on-failure
```

**Every feature PR:** unit tests; curl social alarm scenario; one Chrome extension pass; vision change ⇒ one fake-video run.

**Pre-release soak:** Focus ON 30+ minutes, normal browsing, no crash; Activity Monitor CPU reasonable at 2–5 FPS capture.

### 14.11 Definition of done (local Mac QA)

1. `ctest` passes for core policies.
2. Extension (or curl) logs URLs into SQLite with Focus ON.
3. Social tab → continuous alarm → close tab → stop.
4. Fake phone video or live hold trips and clears phone policy.
5. Focus OFF shows session stats consistent with DB.
6. Relaunch with preserved `FOCUSGAZE_DATA_DIR` does not corrupt sessions.

---

## 16. Risks and mitigations

| Risk | Mitigation |
|------|------------|
| No native “all URLs” API on Mac | Extension + local HTTP as primary |
| Phone detection false positives | Multi-frame consensus, sensitivity slider, cooldown |
| Battery / CPU | Low FPS, inference throttle, pause when idle/lid closed |
| Privacy concerns | Local-only default, no video store, optional URL redaction |
| Gatekeeper blocks unsigned app | Sign + notarize in Phase 5; document right-click Open for dev |
| Alarm ignored (mute / DND) | Always-on-top overlay + sound; condition-based dismiss only |
| YouTube work vs waste | User allowlist / treat as neutral by default |
| TCC differs Terminal vs `.app` | Final QA on installed bundle ID |

---

## 17. Open decisions (lock before or during Phase 0–2)

1. **UI toolkit:** Qt 6 (recommended) vs Dear ImGui.
2. **Blocklist editing:** defaults only in MVP vs full settings UI day one.
3. **Phone alarm clear:** policy A (leave frame) vs B (timed) — **recommend A**.
4. **Browsers in v1:** Chrome only vs Chrome + Safari (Safari needs different distribution story — see **§8**).
5. **Distribution:** personal unsigned DMG first vs Developer ID from the start; extension via Web Store + user Accept (not silent force-install for consumers).
6. **Cloud:** never in product vision vs optional later sync.
7. **Alarm on multiple reasons:** single combined overlay vs stacked reasons list — **recommend stacked reasons in one overlay**.

---

## 18. Immediate next implementation steps

When implementation begins in this repository:

1. Scaffold CMake + Qt tray app + Focus toggle + SQLite (`PlatformPaths` Mac).
2. Local HTTP bridge + Chrome extension MVP + token auth.
3. Social blocklist + sticky `AlarmController` + overlay.
4. Session stats on Focus OFF.
5. Vision pipeline + `PhonePresenceTracker`.
6. Mac packaging (bundle, DMG, signing path).

Keep Windows adapter files as **stubs** from Phase 0 so portability stays visible in the tree.

---

## 19. Document maintenance

- Update this file when policy defaults, schema, or phase exit criteria change.
- Prefer small PRs aligned to phases (0 → 6).
- Core behavior changes require unit tests in the same change set.

---

*End of implementation document.*
