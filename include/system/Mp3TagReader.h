#pragma once

#include <Arduino.h>
#include <FS.h>

/** Read MP3 title from ID3v2 (TIT2) or ID3v1 tags. No heap use. */
class Mp3TagReader {
public:
    static bool readTitle(File& file, char* out, size_t outLen);
};
