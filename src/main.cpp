#include "Arduino.h"
#include "WiFi.h"
#include "time.h"
#include <sys/time.h>
#include <nvs_flash.h>
#include <nvs.h>
#include <Wire.h>
#include "SpiArchitecture.h"
#include "StorageManager.h"
#include "TimeManager.h"
#include "AppLog.h"
#include "PrayerScheduler.h"
#include "AudioManager.h"
#include "AppTypes.h"
#include "SettingsStore.h"
#include "StorageJobQueue.h"
#include "system/BluetoothManager.h"
#include "system/SystemCoordinator.h"
#include "system/AzanSafeMode.h"
#include "system/MemoryGuard.h"
#include "system/MemoryManager.h"
#if !defined(MINI_AZAN_TOUCH_VALIDATION_MODE) || !MINI_AZAN_TOUCH_VALIDATION_MODE
#include "ui/UiBridge.h"
#include "ui/AppCoordinator.h"
#include "UIManager.h"
#if defined(MINI_AZAN_UI_ENABLE) && MINI_AZAN_UI_ENABLE
#include "ui/UiPanel.h"
#endif
#else
#include "ui/TouchValidationOverlay.h"
#endif

// --- LOG LEVEL DEFINITIONS ---
#define LOG_ERROR    0
#define LOG_WARN     1
#define LOG_INFO     2
#define LOG_DEBUG    3

// --- ПИНОВЕ ЗА ХАРДУЕР ---
#define I2S_LRC        25
#define I2S_BCLK       26
#define I2S_DOUT       27

#define DEFAULT_VOLUME 9

#define DEFAULT_PRAYER_TIMES_FILE "/prayer_times.bin"
#define BYTES_PER_DAY 12

TimeManager timeMgr;
StorageManager storageMgr;
AudioManager audioMgr;
PrayerScheduler prayerSched;
SettingsStore settingsStore;
StorageJobQueue storageJobs;
BluetoothManager bluetoothMgr;
SystemCoordinator sysCoord;

#if !defined(MINI_AZAN_TOUCH_VALIDATION_MODE) || !MINI_AZAN_TOUCH_VALIDATION_MODE
UiBridge uiBridge;
AppCoordinator appCoord;
UIManager uiMgr;
#else
TouchValidationOverlay touchOverlay;
#endif

// Глобални обекти
int timeOffsetMinutes = 0;
bool preFajrEnabled = false; 

char uiSelectedAzan[64] = "";
const char* azanFiles[] = {"/Luhaidan_Azan_1.mp3", "/Bahanan_Azan_1.mp3 ", "/azan3.mp3"};
const int numAzanFiles = 3;
int currentAzanIndex = 0; 
int lastPreFajrDay = -1; 
uint32_t lastAudioDuration = 0;

// --- PRAYER TIMES CACHING ---
DayRecord cachedPrayerTimes;
int cachedPrayerDay = -1;
bool cachedPrayerTimesValid = false;

// --- VOLUME CONTROL AND NVS ---
uint8_t currentVolume = 15;
const uint8_t MIN_VOLUME = 0;
const uint8_t MAX_VOLUME = 21;
const char* NVS_NAMESPACE = "azan_system";
const char* NVS_VOLUME_KEY = "volume";
const char* NVS_PREFAJR_KEY = "prefajr";
const char* NVS_AZAN_IDX_KEY = "azan_idx";

bool wasAudioPlaying = false;

// --- LOGGING AND TIMING VARIABLES ---
unsigned long lastHealthLogMs = 0;
bool isAudioPlaying = false;
unsigned long audioStartTime = 0;
const char* currentPlayingFile = "";
unsigned long lastMinutePrayed = 0;

bool getDayRecordFromBin(int day, DayRecord& record);

void sysLog(int level, const char* tag, const char* message) {
    appLog(level, tag, message);
}

void sysLogf(int level, const char* tag, const char* fmt, ...) {
    char buffer[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    appLog(level, tag, buffer);
}

void moduleLog(int level, const char* tag, const char* message) {
    appLog(level, tag, message);
}

static void syncDebugOffset() {
    timeMgr.setDebugOffsetMinutes(timeOffsetMinutes);
}

static void debugSdLogLine(const char* line) {
    sysLog(LOG_INFO, "DEBUG", line);
}

static bool readDayRecordBridge(int day, DayRecord& out) {
    return getDayRecordFromBin(day, out);
}

const char* activeAzanPath() {
    if (uiSelectedAzan[0] != '\0') return uiSelectedAzan;
    return azanFiles[currentAzanIndex];
}

// --- NVS FUNCTIONS FOR PERSISTENT STORAGE ---
void saveVolumeToNVS(uint8_t volume) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        sysLogf(LOG_ERROR, "NVS", "Failed to open NVS (%s)", esp_err_to_name(err));
        return;
    }
    
    err = nvs_set_u8(nvs_handle, NVS_VOLUME_KEY, volume);
    if (err != ESP_OK) {
        sysLogf(LOG_ERROR, "NVS", "Failed to save volume (%s)", esp_err_to_name(err));
    }
    
    err = nvs_commit(nvs_handle);
    if (err == ESP_OK) {
        sysLogf(LOG_DEBUG, "NVS", "Volume saved: %d", volume);
    }
    nvs_close(nvs_handle);
}

void savePreFajrToNVS(bool enabled) {
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, NVS_PREFAJR_KEY, enabled ? 1 : 0);
    nvs_commit(h);
    nvs_close(h);
}

void loadUiPrefsFromNVS() {
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) return;
    uint8_t pf = 0;
    if (nvs_get_u8(h, NVS_PREFAJR_KEY, &pf) == ESP_OK) preFajrEnabled = (pf != 0);
    uint8_t az = 0;
    if (nvs_get_u8(h, NVS_AZAN_IDX_KEY, &az) == ESP_OK && az < (uint8_t)numAzanFiles) {
        currentAzanIndex = az;
    }
    size_t len = sizeof(uiSelectedAzan);
    if (nvs_get_str(h, "azan_path", uiSelectedAzan, &len) != ESP_OK) {
        uiSelectedAzan[0] = '\0';
    }
    nvs_close(h);
}

void saveAzanPathToNVS(const char* path) {
    if (!path) return;
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, "azan_path", path);
    nvs_commit(h);
    nvs_close(h);
}

void saveAzanIndexToNVS(uint8_t index) {
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, NVS_AZAN_IDX_KEY, index);
    nvs_commit(h);
    nvs_close(h);
}

uint8_t loadVolumeFromNVS() {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        sysLogf(LOG_WARN, "NVS", "Failed to open NVS for reading (%s), using default volume 9", esp_err_to_name(err));
        return DEFAULT_VOLUME; // Return default if NVS can't be opened
    }
    
    uint8_t volume = 15;
    err = nvs_get_u8(nvs_handle, NVS_VOLUME_KEY, &volume);
    if (err == ESP_OK) {
        sysLogf(LOG_INFO, "NVS", "Volume loaded from NVS: %d", volume);
    } else if (err == ESP_ERR_NVS_NOT_FOUND) {
        sysLogf(LOG_DEBUG, "NVS", "Volume not found in NVS, using default: 15");
    }
    nvs_close(nvs_handle);
    
    return (volume >= MIN_VOLUME && volume <= MAX_VOLUME) ? volume : 15;
}

void handleDebugConsole();
void printStatus();
void printHealthStatus();
void playAudioFile(const char* filePath);
const char* activeAzanPath();
bool getDayRecordFromBin(int day, DayRecord& record);
String minutesToTime(int totalMinutes);
static bool readDayRecordBridge(int day, DayRecord& out);
static void debugSdLogLine(const char* line);

// ─── Prayer scheduler hook functions ─────────────────────────────
static void prayerPlayAzan() {
    audioMgr.requestPlay(activeAzanPath());
}

static bool prayerIsAudioPlaying() {
    return audioMgr.isRunning();
}

static void prayerSetAudioPlaying(bool playing) {
    isAudioPlaying = playing;
}

static void prayerSetCurrentFile(const char* file) {
    currentPlayingFile = file;
}

void setup() {
    // Initialize serial for logging
    Serial.begin(115200);
    delay(500);
    
    appLogf(APP_LOG_INFO, "BOOT", "=== MiniAzan v1 Starting ===");
    
    // CRITICAL: MemoryManager::begin() must be called FIRST, before any other initialization.
    // It allocates all pools from heap in size-descending order to prevent fragmentation.
    appLog(APP_LOG_INFO, "BOOT", "Initializing memory pools...");
    MemoryManager::begin();
    
    // Initialize core systems
    appLog(APP_LOG_INFO, "BOOT", "Initializing storage...");
    storageMgr.begin(appLog);
    
    appLog(APP_LOG_INFO, "BOOT", "Initializing time manager...");
    TimeManager::Config timeCfg{};
    timeMgr.begin(timeCfg, appLog);
    
    appLog(APP_LOG_INFO, "BOOT", "Initializing UI bridge...");
    uiBridge.begin();
    
    appLog(APP_LOG_INFO, "BOOT", "Initializing app coordinator...");
    AppServices appSvc{};
    appSvc.audio = &audioMgr;
    appSvc.storage = &storageMgr;
    appSvc.time = &timeMgr;
    appSvc.storageJobs = &storageJobs;
    appSvc.bluetooth = &bluetoothMgr;
    appSvc.isAudioPlaying = &isAudioPlaying;
    appSvc.preFajrEnabled = &preFajrEnabled;
    appSvc.currentVolume = &currentVolume;
    appSvc.minVolume = MIN_VOLUME;
    appSvc.maxVolume = MAX_VOLUME;
    appSvc.currentAzanIndex = &currentAzanIndex;
    appSvc.numAzanFiles = numAzanFiles;
    appSvc.azanFiles = azanFiles;
    appSvc.uiSelectedAzanPath = uiSelectedAzan;
    appSvc.uiSelectedAzanPathSize = sizeof(uiSelectedAzan);
    appSvc.prayerBinPath = DEFAULT_PRAYER_TIMES_FILE;
    appSvc.prayerRecordSize = BYTES_PER_DAY;
    appSvc.cachedPrayerTimes = &cachedPrayerTimes;
    appSvc.cachedPrayerDay = &cachedPrayerDay;
    appSvc.cachedPrayerTimesValid = &cachedPrayerTimesValid;
    appSvc.saveVolumeToNvs = saveVolumeToNVS;
    appSvc.savePreFajrToNvs = savePreFajrToNVS;
    appSvc.saveAzanIndexToNvs = saveAzanIndexToNVS;
    appSvc.saveAzanPathToNvs = saveAzanPathToNVS;
    appSvc.readDayRecord = readDayRecordBridge;
    appCoord.begin(uiBridge, appSvc);
    
    appLog(APP_LOG_INFO, "BOOT", "Initializing audio manager...");
    AudioManager::Config audioCfg{};
    audioCfg.pins = {I2S_BCLK, I2S_LRC, I2S_DOUT};
    audioCfg.defaultVolume = DEFAULT_VOLUME;
    audioMgr.begin(storageMgr, audioCfg, appLog);
    

    
    appLog(APP_LOG_INFO, "BOOT", "Initializing Bluetooth manager...");
    bluetoothMgr.begin();
    
    appLog(APP_LOG_INFO, "BOOT", "Initializing prayer scheduler...");
    PrayerScheduler::Config prayerCfg{};
    prayerCfg.prayerBinPath = DEFAULT_PRAYER_TIMES_FILE;
    prayerCfg.recordSize = BYTES_PER_DAY;
    PrayerScheduler::Hooks prayerHooks{};
    prayerHooks.playAzan = prayerPlayAzan;
    prayerHooks.isAudioPlaying = prayerIsAudioPlaying;
    prayerHooks.setAudioPlaying = prayerSetAudioPlaying;
    prayerHooks.setCurrentFile = prayerSetCurrentFile;
    prayerHooks.getAzanPath = activeAzanPath;
    prayerHooks.preFajrEnabled = &preFajrEnabled;
    prayerHooks.lastPreFajrDay = &lastPreFajrDay;
    prayerSched.begin(timeMgr, storageMgr, prayerCfg, prayerHooks);
    
    appLog(APP_LOG_INFO, "BOOT", "Initializing system coordinator...");
    SystemCoordinator::Config sysCoordCfg{};
    sysCoord.begin(uiBridge, appCoord, timeMgr, prayerSched, bluetoothMgr, sysCoordCfg);
    sysCoord.setIsAudioPlayingPtr(&isAudioPlaying);
    
    // Load saved preferences
    currentVolume = loadVolumeFromNVS();
    loadUiPrefsFromNVS();
    
#if !defined(MINI_AZAN_TOUCH_VALIDATION_MODE) || !MINI_AZAN_TOUCH_VALIDATION_MODE
    appLog(APP_LOG_INFO, "BOOT", "Initializing UI...");
    uiMgr.begin(uiBridge);
#else
    appLog(APP_LOG_INFO, "BOOT", "Initializing touch validation overlay...");
    touchOverlay.begin();
#endif

    appLog(APP_LOG_INFO, "BOOT", "=== Boot complete ===");
}

void loop() {
#if !defined(MINI_AZAN_TOUCH_VALIDATION_MODE) || !MINI_AZAN_TOUCH_VALIDATION_MODE
    uiMgr.poll();
#else
    timeMgr.update();
    prayerSched.update();
#endif
    handleDebugConsole();
#if defined(MINI_AZAN_TOUCH_VALIDATION_MODE) && MINI_AZAN_TOUCH_VALIDATION_MODE
    touchOverlay.update();
#endif

    bool audioRunning = audioMgr.isRunning();
    if (isAudioPlaying && wasAudioPlaying && !audioRunning) {
        isAudioPlaying = false;
        appLogf(APP_LOG_INFO, "AUDIO", "azan_done duration_ms=%lu file=%s",
                (unsigned long)(millis() - audioStartTime),
                currentPlayingFile[0] ? currentPlayingFile : "n/a");
    }
    wasAudioPlaying = audioRunning;

    if (millis() - lastHealthLogMs >= 60000) {
        lastHealthLogMs = millis();
        printHealthStatus();
    }

    yield();
}

void handleDebugConsole() {
    if (Serial.available() > 0) {
        String input = Serial.readStringUntil('\n');
        input.trim();
        if (input.length() == 0) return;

        if (input == "STATUS") {
            printStatus();
        }
        else if (input == "OFFSET:0") {
            timeOffsetMinutes = 0;
            syncDebugOffset();
            appLog(APP_LOG_INFO, "CMD", "virt_offset_min=0");
            printStatus();
        }
        else if (input.startsWith("+") || input.startsWith("-")) {
            int oldOffset = timeOffsetMinutes;
            timeOffsetMinutes += input.toInt();
            syncDebugOffset();
            appLogf(APP_LOG_INFO, "CMD", "virt_offset_min=%d delta=%d", timeOffsetMinutes, timeOffsetMinutes - oldOffset);
            printStatus();
        }
        else if (input == "PRAYERLOG") {
            prayerSched.logSchedule();
        }
        else if (input == "CLKDIAG") {
            timeMgr.update();
        }
#if defined(MINI_AZAN_TOUCH_VALIDATION_MODE) && MINI_AZAN_TOUCH_VALIDATION_MODE
        else if (input == "clear") {
            touchOverlay.begin();
            appLog(APP_LOG_INFO, "TVAL", "screen cleared");
        }
#elif defined(MINI_AZAN_UI_ENABLE) && MINI_AZAN_UI_ENABLE
        else if (input == "TOUCHDBG") {
            UiPanel::instance().runTouchDiagnostics();
        }
#endif
        else if (input == "LISTFILES") {
            sysLog(LOG_INFO, "DEBUG", "Listing audio files and their status:");
            for (int i = 0; i < numAzanFiles; i++) {
                bool exists = storageMgr.fileExists(azanFiles[i]);
                uint32_t size = exists ? storageMgr.fileSize(azanFiles[i]) : 0;
                sysLogf(LOG_INFO, "DEBUG", "  [%d] %s -> Exists: %s | Size: %lu bytes", 
                       i, azanFiles[i], exists ? "YES" : "NO", size);
            }
        }
        else if (input == "SDLS") {
            sysLog(LOG_INFO, "DEBUG", "=== All files on SD card root: ===");
            if (!storageMgr.isReady()) {
                sysLog(LOG_ERROR, "DEBUG", "SD Card not initialized!");
                return;
            }
            int count = storageMgr.listRootFilesDebug(debugSdLogLine);
            if (count < 0) {
                sysLog(LOG_ERROR, "DEBUG", "Cannot list SD root (busy or error)");
            } else {
                sysLogf(LOG_INFO, "DEBUG", "Total files: %d", count);
            }
        }
        else if (input.startsWith("PLAYTEST:")) {
            String filename = input.substring(9);
            filename.trim();
            sysLogf(LOG_INFO, "DEBUG", "Testing audio playback: %s", filename.c_str());
            playAudioFile(filename.c_str());
        }
        else if (input == "AUDIOINFO") {
            sysLogf(LOG_INFO, "DEBUG", "Audio Info: isRunning=%d | isPlaying=%d | Volume=%d",
                   audioMgr.isRunning(), isAudioPlaying, currentVolume);
        }
        else if (input == "MEM") {
            MemoryGuard::logHeapStatus("DEBUG");
        }
        else if (input == "POOLSTATS") {
            MemoryManager::logStats("DEBUG");
        }
        else {
            sysLogf(LOG_WARN, "DEBUG", "Unknown command: '%s'", input.c_str());
            appLog(APP_LOG_INFO, "CMD", "help: STATUS OFFSET:0 +N/-N PRAYERLOG CLKDIAG TOUCHDBG LISTFILES SDLS PLAYTEST:x MEM AUDIOINFO");
        }
    }
}

void printStatus() {
    PrayerNow now{};
    if (!timeMgr.getPrayerNow(now) || !now.valid) {
        appLog(APP_LOG_ERROR, "STAT", "clock_unavailable");
        return;
    }

    struct tm sysTm{};
    bool sysOk = timeMgr.readSystemTime(sysTm);
    char sysBuf[12] = "n/a";
    if (sysOk) TimeManager::formatTm(sysTm, sysBuf, sizeof(sysBuf));

    appLogf(APP_LOG_INFO, "STAT",
            "rtc_clock=%02d:%02d:%02d virt_off=%d yday=%d sys=%s drift_sec=%d",
            now.hour, now.minute, now.second, now.debugOffsetMinutes, now.yday,
            sysBuf, timeMgr.getSystemDriftSec());
    appLogf(APP_LOG_INFO, "STAT", "offline=1 bt=%s sd=%s audio=%s src=%s prefajr=%s azan=%s",
            bluetoothMgr.isEnabled() ? "xfer" : "off",
            storageMgr.isReady() ? "ok" : "fail",
            isAudioPlaying ? "play" : "idle",
            timeMgr.rtcUsable() ? "RTC"
                    : (timeMgr.activeSource() == TimeManager::Source::NtpFallback ? "NTP" : "NONE"),
            preFajrEnabled ? "on" : "off",
            activeAzanPath());
    int next = prayerSched.minutesToNextPrayer();
    if (next >= 0) {
        appLogf(APP_LOG_INFO, "STAT", "next_prayer_in_min=%d", next);
    }
}

bool getDayRecordFromBin(int day, DayRecord &record) {
    return storageMgr.readRecordAt(
        DEFAULT_PRAYER_TIMES_FILE, day, &record, sizeof(DayRecord));
}

String minutesToTime(int totalMinutes) {
    if (totalMinutes < 0) totalMinutes += 1440; 
    int hours = totalMinutes / 60;
    int minutes = totalMinutes % 60;
    char buf[6];
    sprintf(buf, "%02d:%02d", hours, minutes);
    return String(buf);
}

// --- HEALTH STATUS MONITORING ---
void printHealthStatus() {
    bool audioRunning = audioMgr.isRunning();
    int next = prayerSched.minutesToNextPrayer();

    appLogf(APP_LOG_INFO, "HEALTH",
            "heap_free=%lu largest=%lu tasks=%lu bt=%s sd=%s audio=%s rtc_age_ms=%lu drift_sec=%d next_min=%d",
            (unsigned long)MemoryGuard::freeHeap(),
            (unsigned long)MemoryGuard::largestFreeBlock(),
            (unsigned long)MemoryGuard::taskCount(),
            bluetoothMgr.isEnabled() ? "xfer" : "off",
            storageMgr.isReady() ? "ok" : "fail",
            audioRunning ? "run" : "idle",
            (unsigned long)timeMgr.getLastRtcReadAgeMs(),
            timeMgr.getSystemDriftSec(), next);
}

// --- WRAPPER TO PLAY AUDIO WITH DETAILED LOGGING ---
void playAudioFile(const char* filePath) {
    MemoryGuard::logHeapStatus("AZAN");
    if (audioMgr.requestPlay(filePath)) {
        isAudioPlaying = true;
    } else {
        isAudioPlaying = false;
    }
}