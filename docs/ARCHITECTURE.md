# MiniAzan — Production embedded architecture

## 1. Hardware ownership (single owner per resource)

| Resource | Owner | Bus / notes |
|----------|--------|-------------|
| SD card | `StorageManager` + `StorageJobQueue` (SDJob task) | VSPI only |
| MP3 / I2S | `AudioManager` (AudioTask) | VSPI read via playback lock |
| TFT + LVGL | `UIManager` / `UiPanel` (LVGL task) | HSPI only |
| Touch | `UiPanel` (LVGL `read_cb`) | HSPI only |
| RTC | `TimeManager` | I2C |
| WiFi / AsyncTCP | `NetworkManager` | No permanent background connect |
| SPI buses | `SpiArch` | VSPI ≠ HSPI, never cross |

**Rule:** No module touches another module’s hardware directly.

## 2. Task map

```
Core 0                          Core 1
──────────────────────────────  ──────────────────────────────
AudioTask      prio 5           LVGL task       prio 1
  queue: PLAY/STOP/VOL            UiBridge in/out only
  MP3 decode + I2S                TFT flush + touch

SDJob          prio 2           SysCoord task   prio 3
  list/delete only                AppCoordinator::poll
  result queue → SysCoord         TimeManager::update
                                  PrayerScheduler::update
                                  NetworkManager::poll

(Arduino loop) prio 1             same core as SysCoord
  debug console, HW button        light: wifiIsOn sync, uiMgr.poll stub
```

| Task | Stack (words) | ~RAM |
|------|----------------|------|
| AudioI2S | 16384 | ~64 KB |
| SDJob | 4096 | ~16 KB |
| SysCoord | 8192 | ~32 KB |
| LVGL | 12288 | ~48 KB |

## 3. Queue / event flow

```
┌─────────────┐  UiCommand   ┌──────────┐  dispatch   ┌─────────────────┐
│ LVGL + UI   │ ──────────► │ UiBridge │ ──────────► │ AppCoordinator  │
│ (no SD/audio)│ ◄────────── │ events   │ ◄────────── │ (SysCoord task) │
└─────────────┘              └──────────┘              └────────┬────────┘
                                                                  │
                    ┌─────────────────────────────────────────────┼──────────────────┐
                    ▼                     ▼                       ▼                  ▼
            StorageJobQueue         AudioManager.request*    NetworkManager    TimeManager
            (submit job)            (cmd queue)              (async WiFi)      (RTC)
                    │
                    ▼
              SDJob worker
                    │
                    ▼ result queue
              AppCoordinator::poll → UiBridge event → LVGL
```

### UI commands (`UiTypes.h`)

- `StopAudio`, `PlayFile`, `SetVolume`, `SetPreFajr`, `SelectAzanFile`
- `ListFolder`, `DeleteFile`, `ToggleWifi`, `RequestClock`, …

## 4. Azan safe mode

When `AudioManager` starts decode:

1. `AzanSafeMode::enter()` — blocks new SD jobs, WiFi start, home UI tick refresh
2. `StorageManager::setPlaybackLocked(true)` — only audio reads VSPI
3. On finish/stop: reverse both

## 5. Memory safety

- `MemoryGuard::canStartMp3Decode()` before `connecttoFS`
- Reject play if `largest_free_block < 42 KB`
- LVGL: single partial buffer **20 lines** (~9.6 KB RGB565)
- `LV_MEM_SIZE` = **28 KB**

## 6. SPI rules

- **VSPI:** SD + audio decode only (mutex in `StorageManager`)
- **HSPI:** TFT + touch; `releaseTftChipSelect()` before touch reads
- No `SD.h` / `Audio.h` in UI sources

## 7. Network rules

- `NetworkManager::requestStart/Stop/Toggle` — non-blocking state machine in `poll()`
- WiFi start **rejected** during azan safe mode
- Web list/delete return 503 while azan playing

## 8. Folder layout

```
include/
  system/          MemoryGuard, AzanSafeMode, NetworkManager, SystemCoordinator
  ui/              UiBridge, AppCoordinator, UiScreens, UiPanel, …
  StorageManager.h, AudioManager.h, StorageJobQueue.h, SpiArchitecture.h
src/
  system/
  ui/
  main.cpp         boot, web routes, thin wrappers
docs/
  ARCHITECTURE.md  (this file)
  UI_ANDROID_SYSTEM.md
```

## 9. Stability checklist

- [ ] Play azan → no SD list crash; `SAFE azan_safe_mode ON` in log
- [ ] File tab while idle → `SDJOB submit/run/done`
- [ ] File tab during azan → submit rejected / StorageBusy
- [ ] MP3 with low heap → `Rejected play — insufficient heap`
- [ ] WiFi toggle from UI → `NET` async logs, no long `delay()` in LVGL task
