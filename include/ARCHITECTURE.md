# MiniAzan SPI & storage architecture

## SPI buses

| Bus | Host | Devices | Owner module |
|-----|------|---------|----------------|
| 1 | **VSPI** | SD card | `StorageManager` |
| 2 | **HSPI** | TFT + touch (future) | `UIManager` |

Pin definitions: `include/SpiArchitecture.h`

## Access rules

1. **SD card** — only `StorageManager` opens `SD` on `SpiArch::sdSpi()` (VSPI).
2. **Audio** — `AudioManager` uses `StorageManager::mediaFs()` for `connecttoFS` only; sets playback lock during decode.
3. **Web / prayer / debug** — use `StorageManager` APIs; never `#include <SD.h>` in `main.cpp`.
4. **UI (future)** — `UIManager` uses `SpiArch::uiSpi()` (HSPI) only; no `SD.h`, no `StorageManager` for direct file I/O.

## ESP32-audioI2S

The library reads the SD filesystem internally during `audio.loop()`. Playback lock blocks other `StorageManager` operations to avoid SPI contention.

## Adding TFT

- Initialize `SpiArch::uiSpi()` inside `UIManager::begin()` only.
- Do not call `SPI.begin()` on VSPI from UI code.
