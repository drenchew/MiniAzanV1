#include "system/MemoryGuard.h"
#include "AppLog.h"
#include <esp_heap_caps.h>

namespace MemoryGuard {

size_t freeHeap() {
    return (size_t)esp_get_free_heap_size();
}

size_t largestFreeBlock() {
    return (size_t)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
}

bool canStartMp3Decode(size_t fileSizeBytes) {
    (void)fileSizeBytes;
    const size_t free = freeHeap();
    const size_t largest = largestFreeBlock();
    if (free < kMp3DecodeMinFree) {
        return false;
    }
    if (largest < kMp3DecodeMinLargest) {
        return false;
    }
    return true;
}

void logHeapStatus(const char* tag) {
    appLogf(APP_LOG_INFO, tag ? tag : "MEM",
            "heap free=%lu largest=%lu min_need=%lu",
            (unsigned long)freeHeap(),
            (unsigned long)largestFreeBlock(),
            (unsigned long)kMp3DecodeMinLargest);
}

}  // namespace MemoryGuard
