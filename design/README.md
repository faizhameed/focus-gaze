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
- **One-click multi-profile install** → `scripts/chrome_extension_installer.py` + tray/dashboard/extension buttons

Stitch HTML under `stitch/` is the design reference (often uses Tailwind CDN). Product code reimplements the look with offline-safe assets.
