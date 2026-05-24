#pragma once

#include <stddef.h>
#include <stdint.h>

namespace MemoryGuard {

constexpr size_t kMp3DecodeMinFree = 48000;
constexpr size_t kMp3DecodeMinLargest = 42000;

size_t freeHeap();
size_t largestFreeBlock();
uint32_t taskCount();

bool canStartMp3Decode(size_t fileSizeBytes = 0);
void logHeapStatus(const char* tag);
void logBootSnapshot();

}  // namespace MemoryGuard
