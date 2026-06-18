#include "system/Mp3TagReader.h"
#include <string.h>

namespace {

uint32_t syncsafe32(const uint8_t* p) {
    return ((uint32_t)(p[0] & 0x7F) << 21) | ((uint32_t)(p[1] & 0x7F) << 14) |
           ((uint32_t)(p[2] & 0x7F) << 7) | (uint32_t)(p[3] & 0x7F);
}

uint32_t be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) |
           (uint32_t)p[3];
}

void trimInPlace(char* s) {
    if (!s || !s[0]) return;
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\0')) {
        s[--len] = '\0';
    }
    char* start = s;
    while (*start == ' ') start++;
    if (start != s) {
        memmove(s, start, strlen(start) + 1);
    }
}

bool copyId3Text(const uint8_t* data, size_t dataLen, char* out, size_t outLen) {
    if (!data || dataLen == 0 || !out || outLen < 2) return false;

    const uint8_t encoding = data[0];
    const uint8_t* text = data + 1;
    size_t textLen = dataLen > 0 ? dataLen - 1 : 0;
    if (textLen == 0) return false;

    size_t outIdx = 0;
    switch (encoding) {
        case 0:  // ISO-8859-1
        case 3:  // UTF-8
            for (size_t i = 0; i < textLen && outIdx + 1 < outLen; i++) {
                const char c = (char)text[i];
                if (c == '\0') break;
                out[outIdx++] = c;
            }
            break;
        case 1:  // UTF-16 with BOM
        case 2:  // UTF-16BE without BOM
            if (textLen >= 2) {
                size_t i = 0;
                if (encoding == 1 && textLen >= 2 &&
                    ((text[0] == 0xFF && text[1] == 0xFE) ||
                     (text[0] == 0xFE && text[1] == 0xFF))) {
                    i = 2;
                }
                for (; i + 1 < textLen && outIdx + 1 < outLen; i += 2) {
                    const char c = (char)text[i + 1];
                    if (text[i] == 0 && c == '\0') break;
                    if (text[i] == 0) out[outIdx++] = c;
                }
            }
            break;
        default:
            return false;
    }

    out[outIdx] = '\0';
    trimInPlace(out);
    return out[0] != '\0';
}

bool readId3v1Title(File& file, char* out, size_t outLen) {
    const size_t fileSize = file.size();
    if (fileSize < 128) return false;

    file.seek(fileSize - 128);
    uint8_t tag[128];
    if (file.read(tag, 128) != 128) return false;
    if (tag[0] != 'T' || tag[1] != 'A' || tag[2] != 'G') return false;

    size_t n = 30;
    if (n >= outLen) n = outLen - 1;
    memcpy(out, tag + 3, n);
    out[n] = '\0';
    trimInPlace(out);
    return out[0] != '\0';
}

bool readId3v2Title(File& file, char* out, size_t outLen) {
    const size_t fileSize = file.size();
    if (fileSize < 10) return false;

    file.seek(0);
    uint8_t hdr[10];
    if (file.read(hdr, 10) != 10) return false;
    if (hdr[0] != 'I' || hdr[1] != 'D' || hdr[2] != '3') return false;

    const uint8_t major = hdr[3];
    const uint32_t tagSize = syncsafe32(hdr + 6);
    if (tagSize == 0 || tagSize > 4096) return false;

    uint8_t buf[256];
    const size_t toRead = tagSize > sizeof(buf) ? sizeof(buf) : tagSize;
    if (file.read(buf, toRead) != toRead) return false;

    size_t pos = 0;
    while (pos + 10 <= toRead) {
        if (buf[pos] == 0) break;

        char frameId[5]{};
        uint32_t frameSize = 0;
        size_t frameHdr = 10;

        if (major == 2) {
            memcpy(frameId, buf + pos, 3);
            frameId[3] = '\0';
            frameSize = ((uint32_t)buf[pos + 3] << 16) | ((uint32_t)buf[pos + 4] << 8) |
                        (uint32_t)buf[pos + 5];
            frameHdr = 6;
        } else {
            memcpy(frameId, buf + pos, 4);
            frameId[4] = '\0';
            frameSize = (major == 4) ? syncsafe32(buf + pos + 4) : be32(buf + pos + 4);
        }

        if (frameSize == 0) break;
        pos += frameHdr;
        if (pos + frameSize > toRead) break;

        const bool isTitle = (strcmp(frameId, "TIT2") == 0) || (strcmp(frameId, "TT2") == 0);
        if (isTitle && copyId3Text(buf + pos, frameSize, out, outLen)) {
            return true;
        }

        pos += frameSize;
    }

    return false;
}

}  // namespace

bool Mp3TagReader::readTitle(File& file, char* out, size_t outLen) {
    if (!out || outLen < 2) return false;
    out[0] = '\0';
    if (!file) return false;

    if (readId3v2Title(file, out, outLen)) return true;
    return readId3v1Title(file, out, outLen);
}
