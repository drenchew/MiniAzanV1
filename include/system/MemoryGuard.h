#pragma once

#include <stddef.h>
#include <stdint.h>

/** Heap checks before MP3 decode / large allocations. */
namespace MemoryGuard {

/** Minimum largest free block to start MP3 decode (ESP32-audioI2S). */
constexpr size_t kMp3DecodeMinFree = 48000;
constexpr size_t kMp3DecodeMinLargest = 42000;

size_t freeHeap();
size_t largestFreeBlock();

bool canStartMp3Decode(size_t fileSizeBytes = 0);
void logHeapStatus(const char* tag);

}  // namespace MemoryGuard
