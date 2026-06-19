#include "system/MemoryGuard.h"
#include "BoardConfig.h"
#include "AppLog.h"
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace MemoryGuard {

size_t freeHeap() {
    return (size_t)esp_get_free_heap_size();
}

size_t largestFreeBlock() {
    return (size_t)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
}

uint32_t taskCount() {
    return (uint32_t)uxTaskGetNumberOfTasks();
}

bool canStartMp3Decode(size_t fileSizeBytes) {
    (void)fileSizeBytes;
    //return freeHeap() >= kMp3DecodeMinFree && largestFreeBlock() >= kMp3DecodeMinLargest;
    return true; // TODO: Remove this override after testing — we want to be sure the heap stats are correct before enforcing them
}

void logHeapStatus(const char* tag) {
    appLogf(APP_LOG_INFO, tag ? tag : "MEM",
            "heap free=%lu largest=%lu tasks=%lu min_largest=%lu",
            (unsigned long)freeHeap(), (unsigned long)largestFreeBlock(),
            (unsigned long)taskCount(), (unsigned long)kMp3DecodeMinLargest);
#if MINI_AZAN_HAS_PSRAM
    appLogf(APP_LOG_INFO, tag ? tag : "MEM",
            "internal free=%lu  psram free=%lu  psram largest=%lu",
            (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
            (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
            (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
#endif
}

void logBootSnapshot() {
    logHeapStatus("BOOT");
}

}  // namespace MemoryGuard
