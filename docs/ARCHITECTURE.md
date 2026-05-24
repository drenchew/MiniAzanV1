# MiniAzan firmware architecture (offline-first)

## Design goals

- Reliable azan playback (audio-first)
- LVGL UI on dedicated task, never blocking on SD or decode
- Single SD owner (`StorageManager` + `StorageJobQueue`)
- WiFi and web stack **removed** — `WiFi.mode(WIFI_OFF)` at boot
- Future file transfer via **Bluetooth Classic SPP** (placeholder layer today)

## Task layout

| Task | Priority | Core | Role |
|------|----------|------|------|
| AudioTask | 5 | 0 | I2S MP3 decode only |
| SDJob | 2 | 0 | Async list/delete on VSPI |
| LVGL (UIManager) | 1 | 1 | Touch + screens |
| SysCoord | 3 | 1 | Time, prayer, AppCoordinator, BT poll |

## Data flow

```
LVGL → UiBridge (commands/events) → AppCoordinator
                                          ├─ AudioManager.request*
                                          ├─ StorageJobQueue.submit
                                          └─ BluetoothManager (future xfer)

Phone/PC → BluetoothManager → AppCoordinator → StorageJobQueue → StorageManager → SD
```

No direct SD writes from Bluetooth callbacks (future).

## Azan safe mode

While azan is playing:

- New SD jobs rejected
- Bluetooth transfer mode start rejected
- Heavy home UI tick refresh skipped

## Memory

Boot and azan start log via `MemoryGuard`:

- `heap free`, `largest free block`, `task count`
- MP3 start gated by `canStartMp3Decode()`

## Modules (`include/system/`)

| Module | Role |
|--------|------|
| `MemoryGuard` | Heap / largest-block / task count logging |
| `AzanSafeMode` | Exclusive mode during playback |
| `BluetoothManager` | Placeholder for SPP file transfer |
| `BluetoothTransferJob` | Future chunked transfer state |
| `SystemCoordinator` | Core-1 poll loop (no LVGL) |

## Removed (legacy)

- `ESPAsyncWebServer`, `AsyncTCP`
- `NetworkManager`, embedded HTML, HTTP upload APIs
- WiFi auto-connect, GPIO4 WiFi toggle, post-prayer WiFi automation
