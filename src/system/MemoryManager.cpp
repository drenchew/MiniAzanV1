/**
 * @file  MemoryManager.cpp
 * @brief Deterministic fixed-pool allocator — global pool definitions + boot.
 *
 * This translation unit is the single point of definition for every pool
 * symbol declared in MemoryManager.h.  All storage is heap-allocated at boot
 * time (via heap_caps_malloc with MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL flags)
 * in size-descending order to prevent fragmentation.
 *
 * Boot sequence requirement
 * ─────────────────────────
 *   MemoryManager::begin()   ← called first in setup()
 *   storageMgr.begin()
 *   audioMgr.begin()
 *   bluetoothMgr.begin()
 *   uiMgr.begin()            ← UiPanel uses gLvglDrawBufA/B
 *
 * After begin() returns, the heap is NOT touched by any of the pools.
 * All BlockSize × BlockCount memory is pre-allocated and stable.
 */

#include "system/MemoryManager.h"
#include "system/AzanSafeMode.h"
#include "AppLog.h"
#include <esp_heap_caps.h>
#include <cstdlib>


// ═════════════════════════════════════════════════════════════════════════════
//  Pool definitions — Heap-allocated at boot via MemoryManager::begin()
//
//  All pools are constructed with heap pointers initialized to nullptr.
//  Actual heap_caps_malloc calls happen in begin(), in size-descending order:
//    1. SdStreamPool      (8192 B)  — largest, prevent fragmentation
//    2. LvglDrawBuf A/B   (19200 B) — DMA-critical, allocate early
//    3. AudioPool         (4096 B)
//    4. BtRxPool          (2048 B)
//    5. BtTxPool          (2048 B)
// ═════════════════════════════════════════════════════════════════════════════

/// P0/P1 — Audio MP3 frame staging (AudioTask exclusive during playback)
HeapBlockPool<MemCfg::AUDIO_BLOCK_SIZE,  MemCfg::AUDIO_BLOCK_COUNT>  gAudioPool;

/// P0/P1 — SD streaming read chunks (AudioTask or StorageJobQueue)
HeapBlockPool<MemCfg::SD_BLOCK_SIZE,     MemCfg::SD_BLOCK_COUNT>     gSdStreamPool;

/// P3 — Bluetooth SPP TX staging (BluetoothManager only)
HeapBlockPool<MemCfg::BT_TX_BLOCK_SIZE,  MemCfg::BT_TX_BLOCK_COUNT>  gBtTxPool;

/// P3 — Bluetooth SPP RX staging (BluetoothManager only)
HeapBlockPool<MemCfg::BT_RX_BLOCK_SIZE,  MemCfg::BT_RX_BLOCK_COUNT>  gBtRxPool;


// ═════════════════════════════════════════════════════════════════════════════
//  LVGL DMA draw buffers
//
//  Allocated on heap at boot with MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL flags
//  to guarantee internal SRAM (not PSRAM) and DMA capability.
//  Pointers initialized to nullptr; set by MemoryManager::begin().
// ═════════════════════════════════════════════════════════════════════════════

uint16_t* gLvglDrawBufA = nullptr;
uint16_t* gLvglDrawBufB = nullptr;


// ═════════════════════════════════════════════════════════════════════════════
//  MemoryManager namespace implementation
// ═════════════════════════════════════════════════════════════════════════════
namespace MemoryManager {

void begin() {
    // ── Allocate pools in size-descending order to prevent fragmentation ─────
    // Each pool is allocated from INTERNAL_SRAM with DMA capability.
    // If any allocation fails, cleanup and log fatal error.

    appLogf(APP_LOG_INFO, "MPOOL",
            "=== MemoryManager boot sequence ===");

    // 1. SdStreamPool — 8192 B (LARGEST, allocate first)
    appLogf(APP_LOG_INFO, "MPOOL", "Allocating SdStreamPool (8192 B)...");
    if (!gSdStreamPool.allocateHeap()) {
        appLogf(APP_LOG_ERROR, "MPOOL",
                "FATAL: SdStreamPool heap allocation failed — device restart required");
        return;
    }
    gSdStreamPool.initFreeList();
    appLogf(APP_LOG_INFO, "MPOOL", "  ✓ SdStreamPool ready  @0x%08x  8192 B",
            (unsigned)gSdStreamPool._blocks[0]);

    // 2. LVGL Draw Buffers — 2 × 9600 B = 19200 B (DMA-critical)
    appLogf(APP_LOG_INFO, "MPOOL", "Allocating LVGL draw buffers (9600 B × 2)...");
    gLvglDrawBufA = static_cast<uint16_t*>(
        heap_caps_malloc(MemCfg::LVGL_DRAW_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL)
    );
    if (!gLvglDrawBufA) {
        appLogf(APP_LOG_ERROR, "MPOOL",
                "FATAL: gLvglDrawBufA heap allocation failed — device restart required");
        gSdStreamPool.cleanup();
        return;
    }
    appLogf(APP_LOG_INFO, "MPOOL", "  ✓ gLvglDrawBufA ready  @0x%08x  9600 B",
            (unsigned)gLvglDrawBufA);

//     gLvglDrawBufB = static_cast<uint16_t*>(
//         heap_caps_malloc(MemCfg::LVGL_DRAW_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL)
//     );
//     if (!gLvglDrawBufB) {
//         appLogf(APP_LOG_ERROR, "MPOOL",
//                 "FATAL: gLvglDrawBufB heap allocation failed — device restart required");
//         heap_caps_free(gLvglDrawBufA);
//         gLvglDrawBufA = nullptr;
//         gSdStreamPool.cleanup();
//         return;
//     }
   // appLogf(APP_LOG_INFO, "MPOOL", "  ✓ gLvglDrawBufB ready  @0x%08x  9600 B",
   //         (unsigned)gLvglDrawBufB);

    // 3. AudioPool — 4096 B
    appLogf(APP_LOG_INFO, "MPOOL", "Allocating AudioPool (4096 B)...");
    if (!gAudioPool.allocateHeap()) {
        appLogf(APP_LOG_ERROR, "MPOOL",
                "FATAL: AudioPool heap allocation failed — device restart required");
        heap_caps_free(gLvglDrawBufA);
      //  heap_caps_free(gLvglDrawBufB);
        gLvglDrawBufA = nullptr;
       // gLvglDrawBufB = nullptr;
        gSdStreamPool.cleanup();
        return;
    }
    gAudioPool.initFreeList();
    appLogf(APP_LOG_INFO, "MPOOL", "  ✓ AudioPool ready  @0x%08x  4096 B",
            (unsigned)gAudioPool._blocks[0]);

    // 4. BtRxPool — 2048 B
    appLogf(APP_LOG_INFO, "MPOOL", "Allocating BtRxPool (2048 B)...");
    if (!gBtRxPool.allocateHeap()) {
        appLogf(APP_LOG_ERROR, "MPOOL",
                "FATAL: BtRxPool heap allocation failed — device restart required");
        heap_caps_free(gLvglDrawBufA);
        //heap_caps_free(gLvglDrawBufB);
        gLvglDrawBufA = nullptr;
      //  gLvglDrawBufB = nullptr;
        gSdStreamPool.cleanup();
        gAudioPool.cleanup();
        return;
    }
    gBtRxPool.initFreeList();
    appLogf(APP_LOG_INFO, "MPOOL", "  ✓ BtRxPool ready  @0x%08x  2048 B",
            (unsigned)gBtRxPool._blocks[0]);

    // 5. BtTxPool — 2048 B
    appLogf(APP_LOG_INFO, "MPOOL", "Allocating BtTxPool (2048 B)...");
    if (!gBtTxPool.allocateHeap()) {
        appLogf(APP_LOG_ERROR, "MPOOL",
                "FATAL: BtTxPool heap allocation failed — device restart required");
        heap_caps_free(gLvglDrawBufA);
        //heap_caps_free(gLvglDrawBufB);
        gLvglDrawBufA = nullptr;
       // gLvglDrawBufB = nullptr;
        gSdStreamPool.cleanup();
        gAudioPool.cleanup();
        gBtRxPool.cleanup();
        return;
    }
    gBtTxPool.initFreeList();
    appLogf(APP_LOG_INFO, "MPOOL", "  ✓ BtTxPool ready  @0x%08x  2048 B",
            (unsigned)gBtTxPool._blocks[0]);

    // ── Boot log: all pools allocated successfully ───────────────────────────
    appLogf(APP_LOG_INFO, "MPOOL",
            "=== MemoryManager ready — %u B heap pools (allocated upfront) ===",
            (unsigned)MemCfg::TOTAL_POOL_BYTES);
    appLogf(APP_LOG_INFO, "MPOOL",
            "All pools allocated in size-descending order to prevent heap fragmentation.");
    appLogf(APP_LOG_INFO, "MPOOL",
            "No further malloc/heap_caps_malloc will be called after this point "
            "(task stacks are one-time boot allocations)");
}


void logStats(const char* tag) {
    const char* t = tag ? tag : "MPOOL";

    appLogf(APP_LOG_INFO, t,
            "sd_pool      free=%u/%u  ok=%lu  fail=%lu  blk=%uB",
            (unsigned)gSdStreamPool.freeCount(),
            (unsigned)gSdStreamPool.totalCount(),
            (unsigned long)gSdStreamPool.acquireOk(),
            (unsigned long)gSdStreamPool.acquireFail(),
            (unsigned)gSdStreamPool.blockSize());

    appLogf(APP_LOG_INFO, t,
            "audio_pool   free=%u/%u  ok=%lu  fail=%lu  blk=%uB",
            (unsigned)gAudioPool.freeCount(),
            (unsigned)gAudioPool.totalCount(),
            (unsigned long)gAudioPool.acquireOk(),
            (unsigned long)gAudioPool.acquireFail(),
            (unsigned)gAudioPool.blockSize());

    appLogf(APP_LOG_INFO, t,
            "bt_rx_pool   free=%u/%u  ok=%lu  fail=%lu  blk=%uB",
            (unsigned)gBtRxPool.freeCount(),
            (unsigned)gBtRxPool.totalCount(),
            (unsigned long)gBtRxPool.acquireOk(),
            (unsigned long)gBtRxPool.acquireFail(),
            (unsigned)gBtRxPool.blockSize());

    appLogf(APP_LOG_INFO, t,
            "bt_tx_pool   free=%u/%u  ok=%lu  fail=%lu  blk=%uB",
            (unsigned)gBtTxPool.freeCount(),
            (unsigned)gBtTxPool.totalCount(),
            (unsigned long)gBtTxPool.acquireOk(),
            (unsigned long)gBtTxPool.acquireFail(),
            (unsigned)gBtTxPool.blockSize());

    appLogf(APP_LOG_INFO, t,
            "lvgl_dma     bufA=0x%08x  bufB=0x%08x  px=%u  bytes=%u",
            (unsigned)reinterpret_cast<uintptr_t>(gLvglDrawBufA),
            //(unsigned)reinterpret_cast<uintptr_t>(gLvglDrawBufB),
            (unsigned)MemCfg::LVGL_DRAW_PIXELS,
            (unsigned)(MemCfg::LVGL_DRAW_BYTES * 2));

    appLogf(APP_LOG_INFO, t,
            "azan_lock=%s  total_heap=%uB",
            AzanSafeMode::isActive() ? "ACTIVE" : "idle",
            (unsigned)MemCfg::TOTAL_POOL_BYTES);
}


bool isAzanActive() {
    return AzanSafeMode::isActive();
}

}  // namespace MemoryManager
