# focusGaze UI designs (Google Stitch)

Designs were generated with **Google Stitch** (`stitch.withgoogle.com` / `stitch.googleapis.com` MCP) for the **focusGaze UI** project.

| Screen | Stitch title | Local assets |
|--------|--------------|--------------|
| Extension popup | focusGaze Popup | `stitch/popup.html`, `stitch/popup.png` |
| Desktop dashboard | focusGaze Dashboard | `stitch/dashboard.html`, `stitch/dashboard.png` |
| Session stats | Last Session Statistics | `stitch/stats.html`, `stitch/stats.png` |
| Alarm overlay | Distraction Alarm Overlay | `stitch/alarm.html`, `stitch/alarm.png` |
| Chrome install | Connect Chrome to focusGaze | `stitch/install.html`, `stitch/install.png` |

**Project resource:** `projects/1087897838876293636`

## Design tokens (implemented in product UI)

| Token | Value |
|-------|-------|
| Background | `#0B0F14` |
| Surface | `#141A22` |
| Border | `#243041` |
| Accent teal | `#2DD4BF` |
| Violet | `#8B5CF6` |
| Text | `#F1F5F9` |
| Muted | `#94A3B8` |
| Danger | `#F87171` |
| Success | `#34D399` |

## Implementation mapping

- **Chrome extension popup** → `extension/chrome/popup.{html,css,js}` (self-contained CSS; no CDN, MV3-safe)
- **Desktop dashboard** → `src/ui/DashboardWindow.*` (Qt Widgets, dark theme)
- **Camera / alarm chrome** → `src/ui/CameraWindow.cpp` styling

Stitch HTML under `stitch/` is the design reference (often uses Tailwind CDN). Product code reimplements the look with offline-safe assets.

## Product UI v2 (Stitch)

Additional screens for shipping UX:

| Screen | Assets |
|--------|--------|
| Statistics dashboard | `stitch/stats.html`, `stitch/stats.png` |
| Onboarding welcome | `stitch/onboarding.html`, `stitch/onboarding.png` |
| Onboarding permissions | `stitch/onboarding_permissions.html`, `stitch/onboarding_permissions.png` |
| Settings | `stitch/settings.html`, `stitch/settings.png` |
| **App logo / brand** | `stitch/logo/` (see below) |

Implemented in-app as Qt pages (Overview / Status / Statistics / Settings) and `OnboardingWizard`.

### App logo (brand mark)

| Asset | Use |
|-------|-----|
| `stitch/logo/logo.svg` | **Master app icon** (vector, Stitch tokens) |
| `stitch/logo/logo-mark.svg` | Circular tray / extension mark |
| `stitch/logo/logo.html` | Design review sheet (tokens + size previews) |
| `stitch/logo/logo-*.png` | Raster exports (128–1024) |
| `stitch/logo/logo_*_concept.jpg` | Concept renders (eye-on-dark, same tokens) |
| `resources/AppIcon.icns` | macOS bundle icon (built into `focusGaze.app`) |
| `resources/icons/appicon_*.png` | Icon pipeline sources |
| `extension/chrome/icons/icon*.png` | Chrome extension action icons |

**Concept:** teal gaze ring + pupil on dark surface, optional violet ambient — matches dashboard primary/secondary.

### Statistics (minimal UI — current product)

Target: **few words, big numbers, three bars**. Design snapshot (for Stitch re-import when MCP auth works):

- `stitch/stats_minimal.html` — sparse layout (Home / Status / Stats / Settings nav; Today·Week·Session·Custom; SCORE + FOCUS; On track / Sites / Phone)
- Golden text snapshot: `testdata/ui/stats_snapshot_golden.txt` (via `StatsViewModel::toSnapshot`)

| UI element | Qt |
|------------|-----|
| 4 chips only | Today, Week, Session, Custom |
| Hero | integer score + focus duration |
| Bars | On track / Sites / Phone (no formula essay) |
| Custom range | single row: from · 📅 · to · 📅 · Apply (popup calendars) |
| Sessions | compact list, not a 5-column table |
