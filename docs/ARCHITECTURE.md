# MiniAzan — deterministic scheduler architecture

## Execution model

All work flows through **AppScheduler** on the SysCoord task (core 1, prio 3):

```
LVGL → UiBridge → AppCoordinator ← AppScheduler::poll()
                                      ├─ P0 STOP_AZAN (fast lane)
                                      ├─ P1 audio commands
                                      ├─ P2 RTC + prayer
                                      ├─ P3 UI settings
                                      └─ P4 SD tick + heavy IO
```

No module runs heavy I/O outside the scheduler except **AudioI2S** (P1 decode on core 0).

## Priority bands (`SchedPriority.h`)

| Band | Name | Examples |
|------|------|----------|
| **P0** | Emergency | `STOP_AZAN` — bypasses deferred queue, `requestEmergencyStop()` |
| **P1** | Real-time | `PlayFile`, audio stop/resume |
| **P2** | System core | RTC update, prayer scheduler, clock/prayer UI refresh |
| **P3** | User action | Volume, pre-Fajr, BT toggle, system status |
| **P4** | Heavy IO | SD list/delete, cooperative `StorageJobQueue::tick()` |

## Azan lock (`AzanSafeMode`)

When azan is playing (`azan_lock = true`):

- Only **P0** and **P1** execute
- SD jobs pause at cooperative checkpoints (`listNextFile` returns `-2`)
- UI keeps rendering; storage commands return `StorageBusy`
- Lock released only when audio stops (EOS or P0 stop)

## P0 STOP path

```
Home STOP button → UiBridge::postUrgent(StopAudio)
  → AppScheduler::runP0FastLane()
    → AppCoordinator::executeEmergencyStop()
      → AudioManager::requestEmergencyStop()  // flush queue + notify AudioTask
```

## Streaming SD (`StorageJobQueue`)

- **No separate SDJob FreeRTOS task** — cooperative state machine driven by P4 `tick()`
- One file entry per tick via `StorageManager::listNextFile()`
- Events: `FileListStreamStart` → `FileListStreamEntry`* → `FileListStreamEnd` + `FileListReady`
- Pauses automatically during azan lock, resumes when lock clears

## Deadlock prevention

- Single SD owner: `StorageManager` mutex, no nested locks
- UI never includes `StorageManager.h` for I/O
- Audio holds playback lock only during decode
- Scheduler never blocks on SD; SD never blocks on LVGL

## Tasks

| Task | Prio | Core | Role |
|------|------|------|------|
| AudioI2S | 5 | 0 | MP3 decode |
| SysCoord | 3 | 1 | AppScheduler |
| LVGL | 1 | 1 | Touch + screens |
| Arduino loop | ~1 | 1 | Health log, serial debug |
