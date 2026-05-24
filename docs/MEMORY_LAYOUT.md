# MiniAzan ESP32 WROOM — Deterministic Memory Layout

> **Design goal:** eliminate all runtime heap fragmentation.  
> After `MemoryManager::begin()` returns, no `malloc`, `new`, or
> `heap_caps_malloc` is called on any runtime path.

---

## 1. ESP32 WROOM RAM Budget

```
┌─────────────────────────────────────────────────────────────────────┐
│            ESP32 WROOM-32 Internal SRAM  (≈ 520 KB nominal)         │
├───────────────────────────┬─────────────────────────────────────────┤
│  IRAM  (128 KB)           │  Code + ISR handlers (read-only)        │
├───────────────────────────┼─────────────────────────────────────────┤
│  DRAM  (≈ 312 KB usable)  │  Data, BSS, stack, FreeRTOS heap        │
│                           │  (ROM / IDF system overhead ≈ 50 KB)    │
└───────────────────────────┴─────────────────────────────────────────┘

No PSRAM on WROOM variant — all buffers must fit in ~260 KB of
effective DRAM after system overhead.
```

---

## 2. Runtime Heap Consumers (before MemoryManager)

| Consumer | Type | Size | Notes |
|---|---|---|---|
| FreeRTOS task: AudioI2S | one-time boot | 64 KB | `taskStackWords=16384 × 4` |
| FreeRTOS task: LVGL | one-time boot | 48 KB | `taskStackWords=12288 × 4` |
| FreeRTOS task: SysCoord | one-time boot | 32 KB | `taskStackWords=8192 × 4` |
| FreeRTOS task: loopTask | one-time boot | ≈ 8 KB | Arduino main loop |
| FreeRTOS task: idle×2 | one-time boot | ≈ 2 KB | one per core |
| AudioManager cmdQueue | one-time boot | ≈ 0.4 KB | `xQueueCreate(4, sizeof(Command))` |
| LVGL internal allocator | static pool | 28 KB | `LV_MEM_SIZE` in lv_conf.h — **not** system heap |
| **LVGL draw buffer** | **runtime** ← **ELIMINATED** | **9.6 KB** | was `heap_caps_malloc` in UiPanel |

After MemoryManager:  
- **Task stacks**: remain as one-time boot allocations (unavoidable in FreeRTOS).  
- **LVGL draw buffers**: moved to BSS static pools.  
- **Zero runtime malloc/new** on any steady-state path.

---

## 3. MemoryManager Pool Layout

All pools reside in **BSS** — allocated by the linker at link time, zero-initialised at chip reset, never touching the heap allocator.

```
DRAM BSS  (MemoryManager static region)
┌────────────────────────────────────────────────────────────────────┐
│  gAudioPool._storage                                               │
│  ┌──────────┬──────────┬──────────┬──────────┬──┬──────────┐     │
│  │  blk[0]  │  blk[1]  │  blk[2]  │  blk[3]  │..│  blk[7]  │     │
│  │  512 B   │  512 B   │  512 B   │  512 B   │  │  512 B   │     │
│  └──────────┴──────────┴──────────┴──────────┴──┴──────────┘     │
│  Total: 4 096 B          align=4               P0/P1 AudioTask    │
├────────────────────────────────────────────────────────────────────┤
│  gSdStreamPool._storage                                            │
│  ┌──────────┬──────────┬──────────┬──────────┬──┬──────────┐     │
│  │  blk[0]  │  blk[1]  │  blk[2]  │  blk[3]  │..│  blk[7]  │     │
│  │  1024 B  │  1024 B  │  1024 B  │  1024 B  │  │  1024 B  │     │
│  └──────────┴──────────┴──────────┴──────────┴──┴──────────┘     │
│  Total: 8 192 B          align=4               P0/P1 → P4 shared  │
├────────────────────────────────────────────────────────────────────┤
│  gBtTxPool._storage                                                │
│  ┌──────────┬──────────┬──────────┬──────────┐                   │
│  │  blk[0]  │  blk[1]  │  blk[2]  │  blk[3]  │                   │
│  │  512 B   │  512 B   │  512 B   │  512 B   │                   │
│  └──────────┴──────────┴──────────┴──────────┘                   │
│  Total: 2 048 B          align=4               P3 BluetoothMgr    │
├────────────────────────────────────────────────────────────────────┤
│  gBtRxPool._storage                                                │
│  ┌──────────┬──────────┬──────────┬──────────┐                   │
│  │  blk[0]  │  blk[1]  │  blk[2]  │  blk[3]  │                   │
│  │  512 B   │  512 B   │  512 B   │  512 B   │                   │
│  └──────────┴──────────┴──────────┴──────────┘                   │
│  Total: 2 048 B          align=4               P3 BluetoothMgr    │
├────────────────────────────────────────────────────────────────────┤
│  gLvglDrawBufA[4800]  (uint16_t, DRAM_ATTR alignas(4))            │
│  ╔════════════════════════════════════════════════════════════╗   │
│  ║  9 600 bytes  ·  240 × 20 pixels × 2 B/px                ║   │
│  ║  DMA-capable — same guarantee as MALLOC_CAP_DMA           ║   │
│  ╚════════════════════════════════════════════════════════════╝   │
├────────────────────────────────────────────────────────────────────┤
│  gLvglDrawBufB[4800]  (uint16_t, DRAM_ATTR alignas(4))            │
│  ╔════════════════════════════════════════════════════════════╗   │
│  ║  9 600 bytes  ·  240 × 20 pixels × 2 B/px                ║   │
│  ║  Double-buffer: LVGL renders to B while A is DMA-flushing ║   │
│  ╚════════════════════════════════════════════════════════════╝   │
├────────────────────────────────────────────────────────────────────┤
│  TOTAL STATIC BSS POOL FOOTPRINT:  35 584 B  ≈  34.75 KB          │
└────────────────────────────────────────────────────────────────────┘
```

---

## 4. Pool Ownership Rules

```
┌──────────────────┬──────────────────────────┬──────────────────────┐
│  Pool            │  Sole Owner              │  Priority Band       │
├──────────────────┼──────────────────────────┼──────────────────────┤
│  gAudioPool      │  AudioTask (Core 0)      │  P0 / P1             │
│                  │  Exclusive during play   │                      │
├──────────────────┼──────────────────────────┼──────────────────────┤
│  gSdStreamPool   │  AudioTask during play   │  P0 / P1 (playback)  │
│                  │  StorageJobQueue idle    │  P4 (background)     │
├──────────────────┼──────────────────────────┼──────────────────────┤
│  gBtTxPool       │  BluetoothManager        │  P3                  │
│                  │  TX path only            │                      │
├──────────────────┼──────────────────────────┼──────────────────────┤
│  gBtRxPool       │  BluetoothManager        │  P3                  │
│                  │  RX path only            │                      │
├──────────────────┼──────────────────────────┼──────────────────────┤
│  gLvglDrawBufA   │  UIManager (pinned)      │  P2 (UI render)      │
│  gLvglDrawBufB   │  Registered at boot,     │                      │
│                  │  never released          │                      │
└──────────────────┴──────────────────────────┴──────────────────────┘
```

**Single-owner pools need no locking** — `gAudioPool` is exclusively used by
`AudioTask` and `gBtTxPool`/`gBtRxPool` exclusively by `BluetoothManager`.
The `portMUX_TYPE` spinlock is present for safety (zero cost when uncontested).

---

## 5. Allocation Rules During Azan Playback

`AzanSafeMode::isActive()` returns `true` for the entire duration of azan playback.  
`MemoryManager::isAzanActive()` is a thin wrapper around it.

```
AzanSafeMode::isActive() == true
        │
        ├─── P0 / P1 callers (AudioTask)
        │         gAudioPool.acquire()    ✓  ALLOWED
        │         gSdStreamPool.acquire() ✓  ALLOWED
        │
        ├─── P2 caller (LVGL UIManager)
        │         gLvglDrawBufA/B         ✓  PINNED — no acquire needed
        │         AzanSafeMode::allowLvglTickRefresh() → true (UI keeps running)
        │
        ├─── P3 caller (BluetoothManager)
        │         if (MemoryManager::isAzanActive()) return nullptr;
        │         gBtTxPool.acquire()     ✗  MUST NOT ACQUIRE — return nullptr
        │         gBtRxPool.acquire()     ✗  MUST NOT ACQUIRE — return nullptr
        │
        └─── P4 caller (StorageJobQueue)
                  gSdStreamPool.acquire() ✗  MUST NOT ACQUIRE — yield/back off
                  AzanSafeMode::allowStorageJobs() → false
```

**No allocation is performed during playback** by any non-P0 subsystem.  
The heap remains untouched — fragmentation is impossible.

---

## 6. StaticBlockPool Internals

```
StaticBlockPool<BlockSize=512, BlockCount=8>

  _storage[8][512]     ← 4 096 bytes, alignas(4), in BSS
  ┌─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┐
  │  0  │  1  │  2  │  3  │  4  │  5  │  6  │  7  │
  └──┬──┴──┬──┴──┬──┴──┬──┴─────┴─────┴─────┴─────┘
     │     │     │     │
  _freeList[8]   ← index stack  (e.g. [0,1,2,3,4,5,6,7] after init)
  _freeTop = 8   ← stack depth
  _mux            ← portMUX_TYPE ESP32 dual-core spinlock

  acquire():                   release(ptr):
    ENTER_CRITICAL               ENTER_CRITICAL
    if _freeTop > 0:               idx = (ptr - _storage) / BlockSize
      idx = _freeList[--_freeTop]   _freeList[_freeTop++] = idx
      return _storage[idx]        EXIT_CRITICAL
    EXIT_CRITICAL

  Complexity: O(1)   Critical section: 3–5 instructions
```

---

## 7. Boot Sequence

```
setup()
  │
  ├─ 1. Serial.begin(115200)
  │
  ├─ 2. MemoryManager::begin()          ← pools initialised here
  │       gAudioPool.init()             ← free-list: [0,1,2,3,4,5,6,7]
  │       gSdStreamPool.init()          ← free-list: [0..7]
  │       gBtTxPool.init()              ← free-list: [0..3]
  │       gBtRxPool.init()              ← free-list: [0..3]
  │       (LVGL buffers: BSS, no init)
  │       → logs pool addresses + total BSS footprint
  │
  ├─ 3. storageMgr.begin()              ← SPI SD init (no pool use)
  ├─ 4. timeMgr.begin()
  ├─ 5. audioMgr.begin()               ← xTaskCreate AudioI2S (heap: stack only)
  ├─ 6. bluetoothMgr.begin()
  ├─ 7. uiMgr.begin()                  ← UiPanel registers gLvglDrawBufA/B
  │                                         NO heap_caps_malloc
  │
  ├─ 8. MemoryGuard::logBootSnapshot() ← snapshot of remaining free heap
  │
  └─ ► loop() — NO malloc/new ever called again
```

---

## 8. LVGL Draw Buffer Migration

| | Before | After |
|---|---|---|
| Allocation | `heap_caps_malloc(9600, MALLOC_CAP_DMA)` | Static BSS `gLvglDrawBufA` |
| DMA guarantee | `MALLOC_CAP_INTERNAL` at runtime | `DRAM_ATTR alignas(4)` at link time |
| Buffer count | 1 (single buffer) | 2 (double buffer — no flush stalls) |
| Failure mode | `nullptr` → boot fail | Cannot fail (BSS always present) |
| Heap impact | −9.6 KB from heap at runtime | 0 (paid at BSS, not heap) |
| Fragmentation | Yes (one allocation after lv_init) | None |

Double-buffering improvement: LVGL renders into `bufB` while `bufA` is being
pushed to the ILI9341 via `tft.pushColors()`.  The DMA transfer and the render
pass overlap — resulting in ~30–40% faster screen refresh on 240×320.

---

## 9. Pool Sizing Rationale

| Pool | Size | Rationale |
|---|---|---|
| `gAudioPool` 512B×8 | 4 KB | One MP3 frame (typical Xing frame ≤ 418 B). 8 blocks cover 3–4 frames of look-ahead. Single-owner → zero lock contention. |
| `gSdStreamPool` 1 KB×8 | 8 KB | Matches FAT32 sector size (512 B) × 2 for alignment. 8 blocks = 8 KB read-ahead window. Shared with StorageJobQueue only when azan is idle. |
| `gBtTxPool` 512B×4 | 2 KB | SPP MTU is 336 B for BT 2.0 classic. 4 blocks = ≥ 1 full BT packet window. Not active during azan. |
| `gBtRxPool` 512B×4 | 2 KB | Symmetric with TX — accommodates one incoming file-transfer chunk. Not active during azan. |
| `gLvglDrawBufA/B` 4800px | 9.6 KB each | `kBufLines=20` lines × 240 pixels × 2 bytes. Matches `UiPanel::kBufLines`. Pinned for device lifetime. |

---

## 10. Invariants and Assertions

The following conditions are enforced at compile time or runtime:

```cpp
// Compile-time: buffer dimensions must stay in sync
static_assert(MemCfg::LVGL_DRAW_LINES == UiPanel::kBufLines,
              "MemCfg::LVGL_DRAW_LINES must equal UiPanel::kBufLines");

// Compile-time: pool count fits in the uint8_t free-list
static_assert(BlockCount <= 32, "BlockCount must be 1..32");

// Runtime: stray-pointer guard in release()
if (p < base || p >= base + sizeof(_storage)) return;

// Runtime: pool exhaustion is non-fatal (returns nullptr)
// Callers MUST handle nullptr — no silent heap fallback
```

---

## 11. Adding a New Pool

1. Add size constants to `namespace MemCfg` in `MemoryManager.h`.
2. Declare `extern StaticBlockPool<Size, Count> gMyPool;` in `MemoryManager.h`.
3. Define `StaticBlockPool<Size, Count> gMyPool;` in `MemoryManager.cpp`.
4. Call `gMyPool.init()` inside `MemoryManager::begin()`.
5. Add a `logPoolLayout("MyPool", gMyPool)` line in `begin()`.
6. Update `TOTAL_POOL_BYTES` in `MemCfg`.
7. Document ownership in this file (section 4) and in `ARCHITECTURE.md`.

**Rule:** a pool MUST have exactly one logical owner.  
If two subsystems need the same buffer space, add a second pool —  
never share a pool between unrelated subsystems.

---

## 12. Memory Map Summary

```
ESP32 WROOM DRAM  (~260 KB effective after IDF overhead)
┌──────────────────────────────────────────────────────────┐  HIGH
│  FreeRTOS heap (runtime allocations)                     │
│    AudioI2S task stack    64 KB  (one-time boot)         │
│    LVGL task stack        48 KB  (one-time boot)         │
│    SysCoord task stack    32 KB  (one-time boot)         │
│    loopTask stack          8 KB  (one-time boot)         │
│    idle task stacks        2 KB  (one-time boot)         │
│    AudioManager cmdQueue  0.4 KB (one-time boot)         │
│    Remaining free heap  ~105 KB  ← no runtime alloc      │
├──────────────────────────────────────────────────────────┤
│  LVGL internal pool (lv_mem.c)    28 KB  (LV_MEM_SIZE)   │
│  [static pool inside lv_mem.c — NOT the system heap]     │
├──────────────────────────────────────────────────────────┤
│  MemoryManager BSS pools          35.5 KB                │
│    gAudioPool                      4.1 KB                │
│    gSdStreamPool                   8.2 KB                │
│    gBtTxPool                       2.1 KB                │
│    gBtRxPool                       2.1 KB                │
│    gLvglDrawBufA + B              19.2 KB                │
├──────────────────────────────────────────────────────────┤
│  Application BSS (globals, objects)  ~10–15 KB           │
│  Application .data (initialised)      ~5 KB              │
└──────────────────────────────────────────────────────────┘  LOW
```

> **MemoryGuard** (`include/system/MemoryGuard.h`) continues to monitor  
> `heap_caps_get_free_size` and `heap_caps_get_largest_free_block` at  
> every azan trigger (`logHeapStatus("AZAN")`) to catch any unexpected  
> third-party library allocations that bypass MemoryManager.
