/**
 * @file  MemoryManager.cpp
 * @brief Deterministic fixed-pool allocator — global pool definitions + boot.
 *
 * This translation unit is the single point of definition for every pool
 * symbol declared in MemoryManager.h.  All storage arrays go into BSS
 * (zero-initialised, never heap-allocated).
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
 * The only remaining heap consumers are FreeRTOS task stacks (one-time
 * boot allocations) and the LVGL internal allocator (LV_MEM_CUSTOM=0,
 * 28 KB static pool managed by lv_mem.c — not the system heap).
 */

#include "system/MemoryManager.h"
#include "system/AzanSafeMode.h"
#include "AppLog.h"


// ═════════════════════════════════════════════════════════════════════════════
//  Pool definitions — BSS section, zero at reset
//
//  The struct layout of StaticBlockPool<> places _storage first, so the
//  4-byte alignment propagates to the first byte of each block within the
//  storage array.  Every block is therefore 4-byte aligned and DMA-capable
//  on ESP32 internal SRAM without heap_caps_malloc.
// ═════════════════════════════════════════════════════════════════════════════

/// P0/P1 — Audio MP3 frame staging (AudioTask exclusive during playback)
StaticBlockPool<MemCfg::AUDIO_BLOCK_SIZE,  MemCfg::AUDIO_BLOCK_COUNT>  gAudioPool;

/// P0/P1 — SD streaming read chunks (AudioTask or StorageJobQueue)
StaticBlockPool<MemCfg::SD_BLOCK_SIZE,     MemCfg::SD_BLOCK_COUNT>     gSdStreamPool;

/// P3 — Bluetooth SPP TX staging (BluetoothManager only)
StaticBlockPool<MemCfg::BT_TX_BLOCK_SIZE,  MemCfg::BT_TX_BLOCK_COUNT>  gBtTxPool;

/// P3 — Bluetooth SPP RX staging (BluetoothManager only)
StaticBlockPool<MemCfg::BT_RX_BLOCK_SIZE,  MemCfg::BT_RX_BLOCK_COUNT>  gBtRxPool;


// ═════════════════════════════════════════════════════════════════════════════
//  LVGL DMA draw buffers
//
//  DRAM_ATTR: forces the symbol into the .dram1.bss section (internal SRAM).
//  On ESP32 WROOM, internal SRAM is always accessible by the DMA engine —
//  this is the same guarantee provided by MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL,
//  achieved here without any runtime allocation.
//
//  alignas(4): guarantees 32-bit alignment required by the TFT_eSPI DMA
//  pushColors() path and the LVGL flush callback.
//
//  Size: 2 × (240 × 20 × 2 bytes) = 19 200 bytes
//  These replace the single heap_caps_malloc call that existed in UiPanel.cpp.
//  Upgrading from single→double buffer also removes LVGL flush stalls.
// ═════════════════════════════════════════════════════════════════════════════

DRAM_ATTR alignas(4) uint16_t gLvglDrawBufA[MemCfg::LVGL_DRAW_PIXELS];
DRAM_ATTR alignas(4) uint16_t gLvglDrawBufB[MemCfg::LVGL_DRAW_PIXELS];


// ═════════════════════════════════════════════════════════════════════════════
//  MemoryManager namespace implementation
// ═════════════════════════════════════════════════════════════════════════════
namespace MemoryManager {

// ── Internal helper ───────────────────────────────────────────────────────────
namespace {

/// Emit one log line per pool showing address range and byte count.
template<size_t BS, size_t BC>
static void logPoolLayout(const char* name,
                          const StaticBlockPool<BS, BC>& pool) {
    const uintptr_t base = reinterpret_cast<uintptr_t>(pool._storage);
    appLogf(APP_LOG_INFO, "MPOOL",
            "  %-14s  @0x%08x  %4u B × %u = %5u B",
            name,
            (unsigned)base,
            (unsigned)BS,
            (unsigned)BC,
            (unsigned)(BS * BC));
}

} // anonymous namespace


// ── Public API ────────────────────────────────────────────────────────────────

void begin() {
    // Initialise free-list stacks and spinlocks for every pool.
    // BSS storage is already zero — init() only sets up book-keeping.
    gAudioPool.init();
    gSdStreamPool.init();
    gBtTxPool.init();
    gBtRxPool.init();
    // LVGL buffers are plain arrays — no init needed; zero at reset.

    // ── Boot log: static layout snapshot ─────────────────────────────────────
    appLogf(APP_LOG_INFO, "MPOOL",
            "=== MemoryManager ready — %u B BSS pools (%u KB) ===",
            (unsigned)MemCfg::TOTAL_POOL_BYTES,
            (unsigned)(MemCfg::TOTAL_POOL_BYTES / 1024));

    logPoolLayout("AudioDecode",  gAudioPool);
    logPoolLayout("SdStream",     gSdStreamPool);
    logPoolLayout("BtTx",         gBtTxPool);
    logPoolLayout("BtRx",         gBtRxPool);

    appLogf(APP_LOG_INFO, "MPOOL",
            "  %-14s  @0x%08x  %5u B  (DMA double-buf)",
            "LvglDrawBufA",
            (unsigned)reinterpret_cast<uintptr_t>(gLvglDrawBufA),
            (unsigned)MemCfg::LVGL_DRAW_BYTES);

    appLogf(APP_LOG_INFO, "MPOOL",
            "  %-14s  @0x%08x  %5u B  (DMA double-buf)",
            "LvglDrawBufB",
            (unsigned)reinterpret_cast<uintptr_t>(gLvglDrawBufB),
            (unsigned)MemCfg::LVGL_DRAW_BYTES);

    appLogf(APP_LOG_INFO, "MPOOL",
            "no malloc/heap_caps_malloc will be called after this point "
            "(task stacks are one-time boot allocations)");
}


void logStats(const char* tag) {
    const char* t = tag ? tag : "MPOOL";

    appLogf(APP_LOG_INFO, t,
            "audio_pool   free=%u/%u  ok=%lu  fail=%lu  blk=%uB",
            (unsigned)gAudioPool.freeCount(),
            (unsigned)gAudioPool.totalCount(),
            (unsigned long)gAudioPool.acquireOk(),
            (unsigned long)gAudioPool.acquireFail(),
            (unsigned)gAudioPool.blockSize());

    appLogf(APP_LOG_INFO, t,
            "sd_pool      free=%u/%u  ok=%lu  fail=%lu  blk=%uB",
            (unsigned)gSdStreamPool.freeCount(),
            (unsigned)gSdStreamPool.totalCount(),
            (unsigned long)gSdStreamPool.acquireOk(),
            (unsigned long)gSdStreamPool.acquireFail(),
            (unsigned)gSdStreamPool.blockSize());

    appLogf(APP_LOG_INFO, t,
            "bt_tx_pool   free=%u/%u  ok=%lu  fail=%lu  blk=%uB",
            (unsigned)gBtTxPool.freeCount(),
            (unsigned)gBtTxPool.totalCount(),
            (unsigned long)gBtTxPool.acquireOk(),
            (unsigned long)gBtTxPool.acquireFail(),
            (unsigned)gBtTxPool.blockSize());

    appLogf(APP_LOG_INFO, t,
            "bt_rx_pool   free=%u/%u  ok=%lu  fail=%lu  blk=%uB",
            (unsigned)gBtRxPool.freeCount(),
            (unsigned)gBtRxPool.totalCount(),
            (unsigned long)gBtRxPool.acquireOk(),
            (unsigned long)gBtRxPool.acquireFail(),
            (unsigned)gBtRxPool.blockSize());

    appLogf(APP_LOG_INFO, t,
            "lvgl_dma     bufA=0x%08x  bufB=0x%08x  px=%u  bytes=%u",
            (unsigned)reinterpret_cast<uintptr_t>(gLvglDrawBufA),
            (unsigned)reinterpret_cast<uintptr_t>(gLvglDrawBufB),
            (unsigned)MemCfg::LVGL_DRAW_PIXELS,
            (unsigned)(MemCfg::LVGL_DRAW_BYTES * 2));

    appLogf(APP_LOG_INFO, t,
            "azan_lock=%s  total_bss=%uB",
            AzanSafeMode::isActive() ? "ACTIVE" : "idle",
            (unsigned)MemCfg::TOTAL_POOL_BYTES);
}


bool isAzanActive() {
    return AzanSafeMode::isActive();
}

}  // namespace MemoryManager
