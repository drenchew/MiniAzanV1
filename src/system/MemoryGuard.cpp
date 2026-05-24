#include "system/MemoryGuard.h"
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
    return freeHeap() >= kMp3DecodeMinFree && largestFreeBlock() >= kMp3DecodeMinLargest;
}

void logHeapStatus(const char* tag) {
    appLogf(APP_LOG_INFO, tag ? tag : "MEM",
            "heap free=%lu largest=%lu tasks=%lu min_largest=%lu",
            (unsigned long)freeHeap(), (unsigned long)largestFreeBlock(),
            (unsigned long)taskCount(), (unsigned long)kMp3DecodeMinLargest);
}

void logBootSnapshot() {
    logHeapStatus("BOOT");
}

}  // namespace MemoryGuard
