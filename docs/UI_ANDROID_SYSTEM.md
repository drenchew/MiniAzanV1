# Android-style LVGL UI — MiniAzan

## Architecture

```
[LVGL task / Core 1]  UiScreens  --UiCommand-->  UiBridge  --poll-->  AppCoordinator (main loop)
                              ^                                              |
                              +----------- UiEvent ---------------------------+
                                              |
                    StorageJobQueue (Core 0)  AudioManager  TimeManager  NVS
```

- **No SD / network / audio I/O in LVGL callbacks** — only `postCommand`.
- **SD list/delete** runs on `SDJob` (~16 KB stack, core 0) via `StorageJobQueue`.
- Worker never touches LVGL; results use a **result queue** drained in `AppCoordinator::poll()`.
- Directory listing uses `listDirectoryPage()` (no `String`/JSON on the worker stack).
- Serial tag `SDJOB` logs submit/run/done + duration for debugging.
- **STOP AZAN** uses urgent queue (`UiCmd::StopAudio`).

## Screens (bottom navigation)

| Tab | Screen | Features |
|-----|--------|----------|
| Home | Dashboard | Clock, date, current/next prayer, countdown bar, STOP |
| Prayer | PrayerTimes | 5 prayer cards, highlight current/next, refresh |
| Azan | AzanSettings | Pre-Fajr toggle, volume 0–100, default azan list from `/azan` |
| Files | FileManager | `/azan` / `/quran` tabs, play (tap), delete (long-press + confirm) |
| BT | Bluetooth | Transfer mode toggle, connection status placeholder, upload progress bar, SD/RTC info |

**Quran Player**: opened from Files → Player; lists `/quran`, tap to play.

## UI commands (`UiTypes.h`)

| Command | Action |
|---------|--------|
| `PlayFile` | Stop + play path via `playAudioFile` (main) |
| `DeleteFile` | Async delete on SD worker |
| `ListFolder` | Async list `/azan`, `/quran`, etc. |
| `SelectAzanFile` | Set default azan + NVS `azan_path` |
| `SetVolume` | 0–100% → maps to 0–21 I2S scale + NVS |
| `ToggleTransferMode` | Enable/disable Bluetooth transfer mode (placeholder) |
| `RequestBluetoothStatus` | Push BT connection / progress events |
| `CancelBluetoothTransfer` | Cancel in-flight transfer (placeholder) |
| `RequestClock` / `RequestPrayerTimes` / `RequestSystemStatus` | Push UI events |

## NVS (`SettingsStore` / `azan_system`)

| Key | Purpose |
|-----|---------|
| `volume` | Audio level (0–21) |
| `prefajr` | Pre-Fajr alarm |
| `azan_idx` | Built-in azan index |
| `azan_path` | Default SD azan file |

(WiFi / `wifi_last` removed — device is offline-first.)

## Touch calibration

Applied in `UiPanel.cpp` (validated XPT2046):  
`x=[554,3672] y=[320,3744] invertY=1`

## Build flags

```ini
-DMINI_AZAN_TOUCH_VALIDATION_MODE=0
-DMINI_AZAN_UI_ENABLE=1
```

Touch lab only: swap to `VALIDATION=1` and `UI_ENABLE=0`.

## Components

- `UiTheme.h` — Material-like colors, card/header styles
- `UiComponents.cpp` — Status bar, bottom nav, confirm dialog, scroll areas
- `UiScreens.cpp` — Screen builders and event handlers
