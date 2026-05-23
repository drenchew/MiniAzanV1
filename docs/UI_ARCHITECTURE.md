# LVGL UI architecture — MiniAzan ESP32

Production-oriented design: **audio never contends with UI on SPI or CPU**, SD only via **StorageManager**, UI never touches I2S or `SD.h`.

---

## 1. Flash & partitions (critical)

### Why you saw ~92% flash

`esp32dev` default partition table allocates **~1.31 MB** for the factory/OTA app slot (`0x140000`). Your linked firmware is **~1.20 MB** → ~92% of the *partition*, not necessarily 92% of the physical 4 MB chip.

### Fix (implemented)

`partitions/miniazan_app.csv` — **3.94 MB** factory app, no OTA slot:

| Region   | Size    |
|----------|---------|
| NVS      | 24 KB   |
| phy_init | 4 KB    |
| **factory (app)** | **0x3F0000 (~3.94 MB)** |

`platformio.ini` → `board_build.partitions = partitions/miniazan_app.csv`

After reflash, PlatformIO “% flash” should drop to ~30% with room for LVGL (~150–350 KB) + screens.

### Memory budget (RAM)

| Consumer        | Target        |
|-----------------|---------------|
| Audio task stack| 16 KB (keep)  |
| LVGL draw buf   | 1× partial (~19 KB @ 240×40 lines RGB565) |
| UiBridge queues | ~2–4 KB       |
| File list cache | 2 KB (names)  |
| WiFi + web      | unchanged     |
| **Reserve**     | ≥80 KB heap free at runtime |

---

## 2. Task architecture (FreeRTOS)

```
Core 0                          Core 1
────────                        ────────
[AudioI2S] prio 5               [LVGL] prio 1  ← lowest
  audio.loop()                    lv_timer_handler()
  VSPI SD (library)               HSPI flush only
                                  touch read → UiBridge

[Main loop] prio 1 (Arduino)    optional: idle
  timeMgr.update()
  AppCoordinator::poll()  ← processes UiBridge, StorageManager, WiFi
  checkAndPlayAzan()      ← prayer logic stays here (deterministic)
```

| Task / loop      | Priority | Core | Must never do        |
|------------------|----------|------|----------------------|
| AudioI2S         | **5**    | 0    | LVGL, WiFi connect block |
| AppCoordinator   | **3**    | 1    | Long SD dir walks in UI task |
| LVGL (UIManager) | **1**    | 1    | SD, `audio.*`, blocking WiFi |
| AsyncTCP / WiFi  | system   | —    | VSPI / TFT SPI       |

**Rule:** Only **one** consumer of VSPI (SD): StorageManager + ESP32-audioI2S during playback. Only **one** consumer of HSPI: UIManager display + touch driver.

---

## 3. Module boundaries

```
┌─────────────┐     post cmd      ┌──────────────────┐     calls      ┌───────────────┐
│  UIManager  │ ────────────────► │    UiBridge      │ ◄───────────── │ AppCoordinator│
│  (LVGL+touch)│ ◄─────────────── │  (FreeRTOS queues)│ ─────────────► │  (prio 3)     │
└─────────────┘     events        └──────────────────┘                └───────┬───────┘
       │ HSPI only                                                      │
       │                                                                ├──► AudioManager (stop only; play via main prayer)
       │                                                                ├──► StorageManager (all SD)
       │                                                                ├──► TimeManager
       │                                                                ├──► NVS (volume, pre-fajr, azan index)
       │                                                                └──► WiFi facade (toggle, status)
```

- **UIManager**: screens, widgets, `lv_timer_handler`, touch → `UiBridge::postCommand()`.
- **UiBridge**: lock-free-ish command queue + event queue (no business logic).
- **AppCoordinator**: sole executor of UI commands; respects `StorageManager::isPlaybackLocked()`.

---

## 4. Screen / state machine

Navigation stack (max depth 4):

| Screen ID      | Purpose |
|----------------|---------|
| `Home`         | Clock, next prayer, WiFi/audio/RTC status, STOP AZAN |
| `Wifi`         | Toggle WiFi, RSSI, IP (read-only + toggle) |
| `Volume`       | Slider 0–21 → NVS |
| `AzanSelect`   | List MP3/WAV from SD (via coordinator list job) |
| `PreFajr`      | Toggle pre-Fajr → NVS |
| `Files`        | List / delete / request upload (upload still via web or future HTTP job) |
| `Settings`     | Optional hub |

Transitions:

- Home → * via bottom nav or buttons.
- Back pops stack; Home clears stack.
- **Modal overlay**: STOP confirm not required — STOP is immediate (see §6).

State stored in `UiScreenMachine` (enum + small stack in `UIManager`).

Data refresh:

- Home: `UiEvent::ClockTick` every 1 s, `UiEvent::PrayerTimes` when coordinator pushes cache.
- Files/Azan: `UiEvent::FileList` after async list completes.

---

## 5. Touch input

- Driver: XPT2046 on HSPI `UI_TOUCH_CS` (or I2C CST if hardware differs — swap driver in `UiPanel.cpp` only).
- Read in **LVGL input dev** `read_cb` (UIManager task): raw ADC → calibrated coords → `lv_indev_data_t`.
- Debounce 20 ms in driver layer; no blocking `delay()` in read_cb.
- Touch never calls SD/audio — only posts `UiCmd::Navigate`, `UiCmd::StopAudio`, etc.

---

## 6. STOP AZAN (instant, audio-safe)

Priority path in `AppCoordinator::poll()`:

1. Drain **high-priority slot** `UiCmd::StopAudio` first (dedicated queue or head-of-line flag).
2. Call `audioMgr.stop()` — does not take SD mutex for decode stop.
3. Post `UiEvent::AudioState` (`Idle`) to UI.

**No** StorageManager call required for stop. Prayer scheduler in `main` may still set `isAudioPlaying`; coordinator clears flags via callback.

Azan **play** from UI: `UiCmd::PlayAzanPreview` only if `!storageMgr.isPlaybackLocked()` and not during prayer auto-play — optional policy in coordinator.

---

## 7. SD access via queue (no LVGL → SD)

UI posts:

| Command | Coordinator action | When blocked |
|---------|-------------------|--------------|
| `ListAudioFiles` | Scan root, filter `.mp3`/`.wav`, cap 32 entries | playback locked → `UiResult::Busy` |
| `DeleteFile` | `storageMgr.removeFile()` | locked → defer/retry |
| `RefreshPrayerBin` | `readRecordAt` for today | locked → defer |
| `UploadFile` | Phase 1: set flag “open web upload”; Phase 2: HTTP chunk via existing web handler | locked → reject |

Long operations:

- Coordinator runs on **core 1**, **prio 3**, time-sliced (max 8 ms per `poll()` slice).
- Yields with `vTaskDelay(1)` between directory entries to avoid starving LVGL completely.

Results → `UiBridge::postEvent(UiEvent::FileList{...})`.

---

## 8. WiFi + NTP in UI

| UI action | Coordinator / main |
|-----------|-------------------|
| Toggle WiFi | Calls existing `toggleWiFi()` (consider non-blocking refactor later) |
| Show status | `WiFi.status()`, `WiFi.localIP()`, RSSI — read only, no connect in LVGL task |
| NTP status | `timeMgr.activeSource()`, `timeMgr.rtcBatterySuspect()` |

On WiFi connect: `timeMgr.setWifiConnected(true)` + `requestNtpSync()` (already non-blocking).

---

## 9. NVS persistence (UI)

| Key | Type | Screen |
|-----|------|--------|
| `volume` | u8 0–21 | Volume (existing) |
| `prefajr` | u8 | Pre-Fajr |
| `azan_idx` | u8 | Azan select |
| `ui_lang` | u8 optional | Settings |

All NVS writes in **AppCoordinator** after UI command — never in LVGL task.

---

## 10. LVGL configuration (flash/RAM)

When enabling (`MINI_AZAN_UI_ENABLE=1`):

- LVGL 8.x (smaller than 9 for ESP32) or 9 with features stripped.
- `LV_COLOR_DEPTH 16`, `LV_MEM_SIZE` 32–48 KB.
- **One** partial buffer: `240 × 40 × 2` bytes.
- No themes heavy assets; built-in material dark minimal.
- Fonts: Montserrat **14 + 20 only** (no full 48 KB set).
- Disable: GPU, large widgets, unnecessary demos.

Libraries (phase 2 in `platformio.ini`):

- `lvgl/lvgl` @ 8.3.x
- `bodmer/TFT_eSPI` (HSPI user_setup) **or** LovyanGFX

Display flush: `flush_cb` → TFT on **HSPI only** (`SpiArch::uiSpi()`).

---

## 11. Risk analysis

| Risk | Cause | Mitigation |
|------|--------|------------|
| Audio stutter | SD list/upload during playback | Playback lock + defer UI SD jobs |
| UI freeze | `toggleWiFi()` blocking delays | Coordinator only; shorten delays later; LVGL prio 1 |
| SPI corruption | TFT + SD same bus | **Already separated** VSPI/HSPI |
| Heap crash | LVGL + audio + WiFi | Partial buffer, limit file list, monitor `ESP.getFreeHeap()` |
| Flash overflow | Default 1.31 MB partition | **miniazan_app.csv** 3.94 MB |
| STOP lag | STOP behind slow queue | Priority STOP slot, polled first |
| Prayer miss | LVGL blocks main | Prayer logic stays in `loop()`, not UI task |
| Web + UI SD race | Both use StorageManager | Mutex + playback lock in StorageManager |

---

## 12. Implementation status (integrated)

| Module | File | Role |
|--------|------|------|
| Display | `src/ui/UiPanel.cpp` | TFT_eSPI ILI9341 flush → LVGL |
| Touch | `src/ui/Xpt2046Touch.cpp` | XPT2046 on HSPI, LVGL pointer input |
| Screens | `src/ui/UiScreens.cpp` | Home, WiFi, Volume, Azan, Pre-Fajr, Files |
| Task | `src/UIManager.cpp` | LVGL task prio 1, core 1 |
| Commands | `src/ui/AppCoordinator.cpp` | SD/WiFi/NVS/audio STOP |

Build: `MINI_AZAN_UI_ENABLE=1`, `lvgl` + `TFT_eSPI`, `include/User_Setup.h`.

**Flash ~34%** of 3.94 MB partition after LVGL (see `platformio run`).

Touch calibration: edit `rawXMin/Max`, `rawYMin/Max` in `Xpt2046Touch::Config` if pointer is off.
