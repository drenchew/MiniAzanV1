/**
 * @file  MemoryManager.h
 * @brief Deterministic fixed-pool allocator — MiniAzan ESP32 WROOM firmware.
 *
 * DESIGN GOAL
 * ───────────
 * Eliminate ALL runtime heap fragmentation by pre-allocating every
 * performance-critical buffer at boot time (heap_caps_malloc, not BSS).
 * Allocations happen in size-descending order to prevent fragmentation.
 * After MemoryManager::begin() completes, no malloc / new / heap_caps_malloc
 * may be called on any runtime path.
 *
 * POOL LAYOUT (Heap, allocated at boot in size-descending order)
 * ─────────────────────────────────────────────────────────────────
 *  Pool              │ Block  │ Count │  Total  │ Owner task/module
 * ───────────────────┼────────┼───────┼─────────┼──────────────────────────
 *  SdStreamPool      │ 1024 B │   8   │  8 192 B│ Allocated FIRST (largest)
 *  LvglDrawBufA/B    │ 9 600 B│   2   │ 19 200 B│ UIManager (LVGL DMA)
 *  AudioDecodePool   │  512 B │   8   │  4 096 B│ AudioTask  (P0 / P1)
 *  BtRxPool          │  512 B │   4   │  2 048 B│ BluetoothManager RX
 *  BtTxPool          │  512 B │   4   │  2 048 B│ BluetoothManager TX
 * ───────────────────┼────────┼───────┼─────────┼──────────────────────────
 *  TOTAL HEAP                           ~35.5 KB
 *  (Allocated at boot in MemoryManager::begin() — zero runtime fragmentation)
 *
 * PRIORITY RULES DURING AZAN PLAYBACK  (AzanSafeMode::isActive() == true)
 * ─────────────────────────────────────────────────────────────────────────
 *  • AudioDecodePool & SdStreamPool: MAY be acquired  (P0/P1 callers)
 *  • BtTxPool / BtRxPool:            MUST NOT acquire  (return nullptr)
 *  • LvglDrawBuf A/B:                pinned at boot — no runtime acquire
 *
 * LOCKING STRATEGY
 * ─────────────────
 *  HeapBlockPool uses portENTER_CRITICAL / portEXIT_CRITICAL
 *  (ESP32 dual-core spinlock, portMUX_TYPE).  The critical section
 *  is 3–5 instructions — safe for P0 callers and ISR contexts.
 *  AudioDecodePool is single-owner (AudioTask) so its lock is
 *  uncontested and effectively free.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <cstdlib>
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>
#include "BoardConfig.h"


// ═════════════════════════════════════════════════════════════════════════════
//  MemCfg — all pool size constants in one place
//  Adjust here only — never scatter magic numbers in source files.
// ═════════════════════════════════════════════════════════════════════════════
namespace MemCfg {

// ── Audio MP3 decode staging (SD → ESP32-audioI2S pipeline) ─────────────────
/// Bytes per audio staging block (one MP3 frame + header headroom)
constexpr size_t AUDIO_BLOCK_SIZE  = 512;
#if MINI_AZAN_HAS_PSRAM
constexpr size_t AUDIO_BLOCK_COUNT = 16;
#else
constexpr size_t AUDIO_BLOCK_COUNT = 12;
#endif

// ── SD streaming read chunks (fatfs reads into these before decode) ──────────
/// Bytes per SD read chunk — matches typical FAT sector multiple
constexpr size_t SD_BLOCK_SIZE  = 1024;
#if MINI_AZAN_HAS_PSRAM
constexpr size_t SD_BLOCK_COUNT = 12;
#else
constexpr size_t SD_BLOCK_COUNT = 8;
#endif

// // ── Bluetooth SPP TX staging ─────────────────────────────────────────────────
// constexpr size_t BT_TX_BLOCK_SIZE  = 512;
// constexpr size_t BT_TX_BLOCK_COUNT = 4;       // 2 048 B total

// // ── Bluetooth SPP RX staging ─────────────────────────────────────────────────
// constexpr size_t BT_RX_BLOCK_SIZE  = 512;
// constexpr size_t BT_RX_BLOCK_COUNT = 4;       // 2 048 B total

// ── LVGL DMA draw buffers (internal SRAM — DMA cannot use PSRAM) ─────────────
/// Display width in pixels (ILI9341 portrait)
constexpr size_t LVGL_DRAW_WIDTH  = 240;
#if MINI_AZAN_HAS_PSRAM
/// Lines per flush — must match UiPanel::kBufLines
constexpr size_t LVGL_DRAW_LINES  = 20;
constexpr size_t LVGL_DRAW_BUF_COUNT = 2;
#else
constexpr size_t LVGL_DRAW_LINES  = 10;
constexpr size_t LVGL_DRAW_BUF_COUNT = 1;
#endif
/// Total pixels per draw buffer
constexpr size_t LVGL_DRAW_PIXELS = LVGL_DRAW_WIDTH * LVGL_DRAW_LINES;
/// Bytes per draw buffer (16 bpp = 2 bytes/pixel)
constexpr size_t LVGL_DRAW_BYTES  = LVGL_DRAW_PIXELS * 2;

// ── Aggregate boot-time pool footprint (informational) ─────────────────────
constexpr size_t TOTAL_POOL_BYTES =
    (AUDIO_BLOCK_SIZE  * AUDIO_BLOCK_COUNT) +
    (SD_BLOCK_SIZE     * SD_BLOCK_COUNT)    +
    (LVGL_DRAW_BYTES   * LVGL_DRAW_BUF_COUNT);

}  // namespace MemCfg


// ═════════════════════════════════════════════════════════════════════════════
//  HeapBlockPool<BlockSize, BlockCount>
//
//  Fixed-capacity pool allocator whose storage is heap-allocated at boot.
//  Allocations happen via heap_caps_malloc(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL)
//  in MemoryManager::begin(), in size-descending order to prevent fragmentation.
//  After begin() returns, no further malloc calls occur.
//
//  Acquire/release are O(1) and spinlock-protected (portMUX_TYPE).
//  The critical section spans 3 instructions — negligible overhead even
//  in the P0 audio path.
//
//  Memory layout:
//    [_storage  BlockCount × BlockSize bytes]  ← 4-byte aligned, DMA-safe
//    [_freeList BlockCount bytes]
//    [_freeTop  1 byte]
//    [_mux      8 bytes  (portMUX_TYPE on ESP32 dual-core)]
//    [counters  12 bytes  (3 × uint32_t)]
// ═════════════════════════════════════════════════════════════════════════════
template<size_t BlockSize, size_t BlockCount>
class HeapBlockPool {

    static_assert(BlockCount >= 1 && BlockCount <= 32,
                  "HeapBlockPool: BlockCount must be 1..32");
    static_assert(BlockSize >= 1,
                  "HeapBlockPool: BlockSize must be >= 1");

public:
    // ── Heap-allocated raw storage (pointer to array of blocks) ──────────────
    uint8_t**    _blocks;            ///< _blocks[idx] → BlockSize bytes
    size_t       _allocatedBlocks;   ///< number of blocks successfully allocated

    // ── Free-list implemented as an index stack ───────────────────────────────
    uint8_t      _freeList[BlockCount];
    uint8_t      _freeTop;           ///< stack depth; == BlockCount when full

    // ── ESP32 dual-core spinlock (portMUX_TYPE) ───────────────────────────────
    portMUX_TYPE _mux;

    // ── Diagnostic counters ───────────────────────────────────────────────────
    uint32_t     _acquireOk;
    uint32_t     _acquireFail;
    uint32_t     _releaseCount;


    // ── Constructor / Destructor ──────────────────────────────────────────────

    HeapBlockPool() : _blocks(nullptr), _allocatedBlocks(0), _freeTop(0) {}
    ~HeapBlockPool() { cleanup(); }


    // ── Heap allocation (called from MemoryManager::begin) ────────────────────

    /**
     * Allocate BlockCount blocks from heap (preferring PSRAM, falling back to internal SRAM).
     * Called exactly once by MemoryManager::begin() in size-descending order.
     *
     * @return true if all allocations succeeded, false if any failed.
     *         On failure, partial allocations are cleaned up.
     */
    bool allocateHeap() {
        // Allocate array of block pointers
        _blocks = static_cast<uint8_t**>(malloc(BlockCount * sizeof(uint8_t*)));
        if (!_blocks) {
            return false;
        }

        size_t allocated = 0;
        for (size_t i = 0; i < BlockCount; ++i) {
            // Audio/SD pools may live in PSRAM on S3; LVGL DMA buffers stay internal.
#if MINI_AZAN_HAS_PSRAM
            _blocks[i] = static_cast<uint8_t*>(
                heap_caps_malloc(BlockSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
            );
            if (!_blocks[i]) {
                _blocks[i] = static_cast<uint8_t*>(
                    heap_caps_malloc(BlockSize, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)
                );
            }
#else
            _blocks[i] = static_cast<uint8_t*>(
                heap_caps_malloc(BlockSize, MALLOC_CAP_DEFAULT)
            );
#endif
            if (!_blocks[i]) {
                // Allocation failed — clean up what we allocated so far
                cleanup();
                return false;
            }
            allocated++;
        }
        _allocatedBlocks = allocated;
        return true;
    }

    /**
     * Initialise pool state and spinlock after heap allocation succeeds.
     * Called by MemoryManager::begin() after allocateHeap() returns true.
     */
    void initFreeList() {
        _mux = portMUX_INITIALIZER_UNLOCKED;
        for (uint8_t i = 0; i < static_cast<uint8_t>(BlockCount); ++i) {
            _freeList[i] = i;
        }
        _freeTop      = static_cast<uint8_t>(BlockCount);
        _acquireOk    = 0;
        _acquireFail  = 0;
        _releaseCount = 0;
    }

    /**
     * Free all heap-allocated blocks.
     * Called on error path or destructor.
     */
    void cleanup() {
        if (_blocks) {
            for (size_t i = 0; i < _allocatedBlocks; ++i) {
                if (_blocks[i]) {
                    heap_caps_free(_blocks[i]);
                    _blocks[i] = nullptr;
                }
            }
            free(_blocks);
            _blocks = nullptr;
            _allocatedBlocks = 0;
        }
    }


    // ── Runtime API ───────────────────────────────────────────────────────────

    /**
     * Acquire one block from the pool.
     *
     * @return Pointer to a BlockSize-byte region, or nullptr if exhausted.
     *
     * Contract:
     *  • Non-blocking, O(1).
     *  • Spinlock-protected — safe from any task or ISR context.
     *  • Caller must not acquire from a non-P0 pool while AzanSafeMode is
     *    active (see MemoryManager::isAzanActive()).
     *  • Returned pointer remains valid until release() is called.
     */
    void* acquire() {
        void* ptr = nullptr;
        portENTER_CRITICAL(&_mux);
        if (_freeTop > 0) {
            --_freeTop;
            ptr = static_cast<void*>(_blocks[_freeList[_freeTop]]);
            ++_acquireOk;
        } else {
            ++_acquireFail;
        }
        portEXIT_CRITICAL(&_mux);
        return ptr;
    }

    /**
     * Release a block back to the pool.
     *
     * @param ptr  Pointer previously returned by acquire() on *this* pool.
     *             Nullptr is silently ignored.  Stray pointers are checked.
     *
     * Contract:
     *  • Non-blocking, O(1).
     *  • Spinlock-protected — safe from any task or ISR context.
     *  • Do NOT release the same block twice — free-list corruption.
     */
    void release(void* ptr) {
        if (!ptr || !_blocks) return;

        // Find which block this pointer belongs to
        for (uint8_t i = 0; i < static_cast<uint8_t>(BlockCount); ++i) {
            if (_blocks[i] == ptr) {
                portENTER_CRITICAL(&_mux);
                if (_freeTop < static_cast<uint8_t>(BlockCount)) {
                    _freeList[_freeTop] = i;
                    ++_freeTop;
                    ++_releaseCount;
                }
                portEXIT_CRITICAL(&_mux);
                return;
            }
        }
    }


    // ── Status accessors (read-only, lock-free) ───────────────────────────────

    uint8_t  freeCount()     const { return _freeTop; }
    uint8_t  totalCount()    const { return static_cast<uint8_t>(BlockCount); }
    bool     empty()         const { return _freeTop == 0; }
    bool     full()          const { return _freeTop == static_cast<uint8_t>(BlockCount); }
    size_t   blockSize()     const { return BlockSize; }
    size_t   poolBytes()     const { return BlockSize * BlockCount; }
    uint32_t acquireOk()     const { return _acquireOk; }
    uint32_t acquireFail()   const { return _acquireFail; }
    uint32_t releaseCount()  const { return _releaseCount; }
};


// ═════════════════════════════════════════════════════════════════════════════
//  Global pool declarations
//  Definitions live in MemoryManager.cpp (one translation unit).
//  All storage is in BSS — zero at reset, no runtime constructor.
// ═════════════════════════════════════════════════════════════════════════════

/** @defgroup AudioPools  P0/P1 — Audio real-time path */
///@{
/// MP3 frame staging — exclusive to AudioTask during playback (no contention).
extern HeapBlockPool<MemCfg::AUDIO_BLOCK_SIZE,  MemCfg::AUDIO_BLOCK_COUNT>  gAudioPool;
/// SD read chunks — AudioTask during play; StorageJobQueue between prayers.
extern HeapBlockPool<MemCfg::SD_BLOCK_SIZE,     MemCfg::SD_BLOCK_COUNT>     gSdStreamPool;
///@}

/** @defgroup BtPools  P3 — Bluetooth transfer path */
///@{
/// BT SPP outbound staging — BluetoothManager only.
//extern HeapBlockPool<MemCfg::BT_TX_BLOCK_SIZE,  MemCfg::BT_TX_BLOCK_COUNT>  gBtTxPool;
/// BT SPP inbound staging — BluetoothManager only.
//extern HeapBlockPool<MemCfg::BT_RX_BLOCK_SIZE,  MemCfg::BT_RX_BLOCK_COUNT>  gBtRxPool;
///@}

/**
 * @defgroup LvglBuffers  LVGL DMA draw buffers
 *
 * Allocated directly on the heap at boot with DMA capabilities.
 * Each buffer is individually malloc'd with MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL
 * to ensure internal SRAM (not PSRAM) and alignment for DMA.
 *
 * UIManager registers them at boot:
 * @code
 *   lv_disp_draw_buf_init(&_drawBuf,
 *                          reinterpret_cast<lv_color_t*>(gLvglDrawBufA),
 *                          reinterpret_cast<lv_color_t*>(gLvglDrawBufB),
 *                          MemCfg::LVGL_DRAW_PIXELS);
 * @endcode
 * After registration the buffers are pinned for the device lifetime —
 * never acquired or released through the pool API.
 */
///@{
extern uint16_t* gLvglDrawBufA;
#if MINI_AZAN_HAS_PSRAM
extern uint16_t* gLvglDrawBufB;
#endif
///@}


// ═════════════════════════════════════════════════════════════════════════════
//  MemoryManager namespace — boot initialisation and diagnostics
// ═════════════════════════════════════════════════════════════════════════════
namespace MemoryManager {

/**
 * Initialise all pool free-lists and log a boot snapshot.
 *
 * MUST be the first call in setup(), before any subsystem begin().
 * Runs synchronously on the Arduino main task.  Not re-entrant.
 */
void begin();

/**
 * Log per-pool statistics: free/total blocks, cumulative acquire/fail counts.
 *
 * Safe to call from any task at any time (reads are spinlock-protected).
 * Typical usage: MEM serial command, health log every 60 s.
 *
 * @param tag  Log tag string (defaults to "MPOOL").
 */
void logStats(const char* tag = "MPOOL");

/**
 * Returns true when AzanSafeMode is active (azan playback in progress).
 *
 * Non-P0 callers (BT, SD background jobs) MUST check this before calling
 * acquire() on BtTx/RxPool or performing any non-critical allocation.
 * AudioPool and SdStreamPool remain available regardless.
 */
bool isAzanActive();

/**
 * Compile-time total static BSS footprint of all pools combined (bytes).
 * Informational — use in boot log / documentation.
 */
constexpr size_t totalPoolFootprint() {
    return MemCfg::TOTAL_POOL_BYTES;
}

}  // namespace MemoryManager




// 46149|-------- --:--:--|I|AUDIO|heap free=24896 largest=17396 tasks=9 min_largest=42000
//120014|-------- --:--:--|I|HEALTH|heap_free=24896 largest=17396 tasks=9 

//|I|HEALTH|heap_free=56264 largest=49140
//                  (5567352 bytes)

//600000|-------- --:--:--|I|HEALTH|heap_free=48248 largest=40948
// -|I|AUDIO|heap free=52608 largest=40948 tasks=9 min_largest=42000