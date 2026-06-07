#include "StorageManager.h"
#include <stdarg.h>

#ifndef LOG_ERROR
#define LOG_ERROR 0
#define LOG_WARN  1
#define LOG_INFO  2
#define LOG_DEBUG 3
#endif

static_assert(SpiArch::SD_HOST == VSPI, "SD card must use VSPI (bus 1) only");
static_assert(SpiArch::UI_HOST == HSPI, "TFT/touch must use HSPI (bus 2) only");

void StorageManager::logf(int level, const char* tag, const char* fmt, ...) const {
    if (!_log) return;
    char buf[192];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    _log(level, tag, buf);
}

bool StorageManager::begin(const Config& cfg, LogFn logFn) {
    _cfg = cfg;
    _log = logFn;
    _ready = false;
    _playbackLocked = false;

    if (_mutex == nullptr) {
        _mutex = xSemaphoreCreateMutex();
    }
    if (_mutex == nullptr) {
        logf(LOG_ERROR, "STORAGE", "Failed to create SD mutex");
        return false;
    }

    SpiArch::BusInitResult bus = SpiArch::initSdBus();
    if (!bus.ok) {
        logf(LOG_ERROR, "STORAGE", "VSPI init failed host=%d expect=%d", bus.hostId, SpiArch::SD_HOST);
        return false;
    }

    SPIClass& spi = SpiArch::sdSpi();
    if (!SD.begin(_cfg.csPin, spi)) {
        logf(LOG_ERROR, "STORAGE", "SD.begin failed on VSPI (CS=%d)", _cfg.csPin);
        return false;
    }

    _ready = true;
    logf(LOG_INFO, "STORAGE", "SD on VSPI host=%d SCK=%d MISO=%d MOSI=%d CS=%d",
         bus.hostId,
         _cfg.sck, _cfg.miso, _cfg.mosi, _cfg.csPin);
    return true;
}

void StorageManager::setPlaybackLocked(bool locked) {
    _playbackLocked = locked;
}

FS& StorageManager::mediaFs() {
    return SD;
}

bool StorageManager::takeLock(TickType_t timeout, bool ignorePlaybackLock) {
    if (!_ready || _mutex == nullptr) return false;
    if (_playbackLocked && !ignorePlaybackLock) return false;
    return xSemaphoreTake(_mutex, timeout) == pdTRUE;
}

void StorageManager::giveLock() {
    if (_mutex != nullptr) xSemaphoreGive(_mutex);
}

String StorageManager::normalizePath(const char* path) {
    if (!path || !path[0]) return String();
    String p(path);
    if (!p.startsWith("/")) p = "/" + p;
    return p;
}

void StorageManager::normalizePathTo(const char* path, char* out, size_t outLen) {
    if (!out || outLen < 2) return;
    if (!path || !path[0]) {
        strncpy(out, "/", outLen - 1);
        out[outLen - 1] = '\0';
        return;
    }
    if (path[0] == '/') {
        strncpy(out, path, outLen - 1);
    } else {
        snprintf(out, outLen, "/%s", path);
    }
    out[outLen - 1] = '\0';
}

int StorageManager::_loadDirectoryCache(const char* dirPath) {
    // Must already hold mutex
    
    char dir[256];
    normalizePathTo(dirPath, dir, sizeof(dir));
    
    // If already cached for this path and cache is fresh (< 10 min), reuse it
    if (_dirCache.count > 0 && 
        strncmp(_dirCache.path, dir, sizeof(_dirCache.path) - 1) == 0 &&
        (millis() - _dirCache.timestamp) < 600000) {  // 10 min TTL
        return _dirCache.count;
    }
    
    // New cache load: clear old and start fresh
    _dirCache.count = 0;
    strncpy(_dirCache.path, dir, sizeof(_dirCache.path) - 1);
    _dirCache.path[sizeof(_dirCache.path) - 1] = '\0';
    _dirCache.timestamp = millis();
    
    File root = SD.open(dir);
    if (!root || !root.isDirectory()) {
        if (root) {
            root.close();
        }
        _dirCache.count = 0;
        return 0;
    }
    
    // Single-pass scan: filter only directories and .mp3 files
    File file = root.openNextFile();
    while (file && _dirCache.count < MAX_CACHE_ENTRIES) {
        bool isDir = file.isDirectory();
        bool isMp3 = false;
        
        if (!isDir) {
            const char* name = file.name();
            if (name) {
                size_t len = strlen(name);
                if (len > 4) {
                    const char* ext = name + len - 4;
                    isMp3 = (strcasecmp(ext, ".mp3") == 0);
                }
            }
        }
        
        if (isDir || isMp3) {
            DirEntry& entry = _dirCache.entries[_dirCache.count];
            const char* full = file.name();
            const char* base = full;
            
            // Extract basename from full path
            if (full) {
                const char* slash = strrchr(full, '/');
                if (slash && slash[1]) {
                    base = slash + 1;
                }
            } else {
                base = "";
            }
            
            strncpy(entry.name, base, sizeof(entry.name) - 1);
            entry.name[sizeof(entry.name) - 1] = '\0';
            entry.size = (uint32_t)file.size();
            entry.isFolder = isDir;
            _dirCache.count++;
        }
        
        file.close();
        file = root.openNextFile();
    }
    
    root.close();
    logf(LOG_INFO, "STORAGE", "Cached dir %s: %d entries", dir, _dirCache.count);
    return _dirCache.count;
}

void StorageManager::_clearCache() {
    _dirCache.count = 0;
    _dirCache.path[0] = '\0';
    _dirCache.timestamp = 0;
}

int StorageManager::listDirectoryPage(const char* dirPath, DirEntry* out, int maxEntries,
                                      int skip, int* totalOut) {
    if (!out || maxEntries <= 0 || !_ready) {
        return 0;
    }
    if (_playbackLocked) {
        return -1;
    }
    if (!takeLock(_cfg.mutexTimeout)) {
        return -1;
    }

    // Initialize output
    for (int i = 0; i < maxEntries; i++) {
        out[i].isFolder = false;
    }

    // Load (or reuse cached) directory contents
    int totalCount = _loadDirectoryCache(dirPath);
    if (totalCount <= 0) {
        giveLock();
        if (totalOut) {
            *totalOut = 0;
        }
        return 0;
    }

    // Serve pagination from cache
    int filled = 0;
    for (int i = skip; i < totalCount && filled < maxEntries; i++) {
        out[filled] = _dirCache.entries[i];
        filled++;
    }

    giveLock();

    if (totalOut) {
        *totalOut = totalCount;
    }
    return filled;
}

int StorageManager::listNextFile(const char* dirPath, int& cursor, DirEntry& out,
                                 int* totalOut) {
    if (_playbackLocked) {
        return -2;
    }
    DirEntry tmp[1];
    const int n = listDirectoryPage(dirPath, tmp, 1, cursor, totalOut);
    if (n < 0) {
        return -1;
    }
    if (n == 0) {
        return 0;
    }
    out = tmp[0];
    cursor++;
    return 1;
}

bool StorageManager::fileExists(const char* path) {
    String full = normalizePath(path);
    if (full.isEmpty()) return false;
    if (!takeLock(_cfg.mutexTimeout)) return false;
    File f = SD.open(full, FILE_READ);
    bool ok = (bool)f;
    if (f) f.close();
    giveLock();
    return ok;
}

uint32_t StorageManager::fileSize(const char* path) {
    String full = normalizePath(path);
    if (full.isEmpty()) return 0;
    if (!takeLock(_cfg.mutexTimeout)) return 0;
    File f = SD.open(full, FILE_READ);
    uint32_t sz = f ? f.size() : 0;
    if (f) f.close();
    giveLock();
    return sz;
}

bool StorageManager::removeFile(const char* path) {
    String full = normalizePath(path);
    if (full.isEmpty()) return false;
    if (!takeLock(_cfg.mutexTimeout)) return false;
    bool ok = SD.remove(full);
    giveLock();
    return ok;
}

bool StorageManager::readRecordAt(const char* binPath, int recordIndex, void* out, size_t recordSize) {
    if (!binPath || recordIndex < 1 || !out || recordSize == 0) return false;
    if (!takeLock(_cfg.mutexTimeout)) return false;

    File in = SD.open(binPath, FILE_READ);
    if (!in) {
        giveLock();
        return false;
    }
    bool ok = false;
    if (in.seek((recordIndex - 1) * recordSize, SeekSet)) {
        ok = (in.read((uint8_t*)out, recordSize) == recordSize);
    }
    in.close();
    giveLock();
    return ok;
}

bool StorageManager::uploadBegin(const char* filename) {
    if (!filename || !filename[0]) return false;
    if (_playbackLocked) return false;
    if (!takeLock(_cfg.mutexTimeout)) return false;

    if (_uploadFile) _uploadFile.close();
    String path = "/" + String(filename);
    _uploadFile = SD.open(path, FILE_WRITE);
    bool ok = (bool)_uploadFile;
    if (!ok) giveLock();
    return ok;
}

bool StorageManager::uploadWrite(const uint8_t* data, size_t len) {
    if (!_uploadFile || len == 0) return false;
    return _uploadFile.write(data, len) == len;
}

void StorageManager::uploadEnd(bool success) {
    if (_uploadFile) {
        _uploadFile.close();
        _uploadFile = File();
    }
    giveLock();
    (void)success;
}

bool StorageManager::listDirectoryJson(const char* dirPath, String& jsonOut) {
    jsonOut = "{\"files\":[";
    if (!_ready || !dirPath) {
        jsonOut += "]}";
        return !_ready;
    }
    if (!takeLock(_cfg.mutexTimeout)) {
        jsonOut += "]}";
        return false;
    }

    String dir = normalizePath(dirPath);
    File root = SD.open(dir.c_str());
    if (!root || !root.isDirectory()) {
        if (root) root.close();
        giveLock();
        jsonOut += "]}";
        return true;
    }

    bool first = true;
    File file = root.openNextFile();
    while (file) {
        if (!file.isDirectory()) {
            if (!first) jsonOut += ",";
            String name = String(file.name());
            int slash = name.lastIndexOf('/');
            if (slash >= 0) name = name.substring(slash + 1);
            jsonOut += "{\"name\":\"" + name + "\",\"size\":" + String(file.size()) + "}";
            first = false;
        }
        file.close();
        file = root.openNextFile();
    }
    root.close();
    giveLock();
    jsonOut += "]}";
    return true;
}

bool StorageManager::listRootFilesJson(String& jsonOut) {
    return listDirectoryJson("/", jsonOut);
}

int StorageManager::listRootFilesDebug(void (*logLine)(const char* line)) {
    if (!logLine || !_ready) return 0;
    if (!takeLock(_cfg.mutexTimeout)) return -1;

    File root = SD.open("/");
    if (!root) {
        giveLock();
        return -1;
    }

    int count = 0;
    File file = root.openNextFile();
    while (file) {
        char line[96];
        if (file.isDirectory()) {
            snprintf(line, sizeof(line), "  [DIR] %s/", file.name());
        } else {
            snprintf(line, sizeof(line), "  [FILE] %s (%lu bytes)", file.name(), (unsigned long)file.size());
            count++;
        }
        logLine(line);
        file.close();
        file = root.openNextFile();
    }
    root.close();
    giveLock();
    return count;
}
