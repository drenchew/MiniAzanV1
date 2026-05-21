#include "Arduino.h"
#include "WiFi.h"
#include "time.h"
#include "SD.h"
#include "FS.h"
#include "SPI.h"
#include "Audio.h"
#include "AsyncTCP.h"
#include "ESPAsyncWebServer.h"

// --- LOG LEVEL DEFINITIONS ---
#define LOG_ERROR    0
#define LOG_WARN     1
#define LOG_INFO     2
#define LOG_DEBUG    3

// --- НАСТРОЙКИ НА WI-FI ---
const char* ssid       = "iPhone";
const char* password   = "abood123";
const char* ntpServer  = "pool.ntp.org";

// --- ПИНОВЕ ЗА ХАРДУЕР ---
#define I2S_LRC        25
#define I2S_BCLK       26
#define I2S_DOUT       22
#define SD_CS          5
#define WIFI_BUTTON_PIN 4  // <--- ПИН за бутона за Wi-Fi (свързан към GND)

#define DEFAULT_PRAYER_TIMES_FILE "/prayer_times.bin"
#define BYTES_PER_DAY 12

struct DayRecord {
    uint16_t times[6]; 
};

// Глобални обекти
Audio audio;
AsyncWebServer server(80);
SemaphoreHandle_t spiMutex = NULL;
bool sdCardInitialized = false;

int timeOffsetMinutes = 0; 
bool preFajrEnabled = false; 

const char* azanFiles[] = {"/Luhaidan_Azan_1.mp3", "/Bahanan_Azan_1.mp3 ", "/azan3.mp3"};
const int numAzanFiles = 3;
int currentAzanIndex = 0; 
int lastPreFajrDay = -1; 
TaskHandle_t AudioTaskHandle = NULL;

// --- AUDIO DEBUG VARIABLES ---
bool lastAudioRunningState = false;
uint32_t lastAudioDuration = 0;

// --- WI-FI УПРАВЛЕНИЕ И ТАЙМЕР ---
bool wifiIsOn = true;
bool autoWifiShutdownDone = false; // Следи дали 5-те минути са минали
const unsigned long AUTO_WIFI_OFF_MS = 5 * 60 * 1000UL; // 5 минути в милисекунди
unsigned long lastButtonPress = 0;
const unsigned long debounceDelay = 300; 

// --- WiFi AUTO ON/OFF AFTER AZAN (POST-PRAYER) ---
bool wasAudioPlaying = false;
unsigned long postPrayerWifiTimer = 0;
bool postPrayerWifiActive = false;
const unsigned long WIFI_AUTO_DURATION_AFTER_AZAN = 5 * 60 * 1000UL; // 5 minutes 

// --- PRAYER TIMES CACHING (read only once per day) ---
DayRecord cachedPrayerTimes;
int cachedPrayerDay = -1;
bool cachedPrayerTimesValid = false;

// --- LOGGING AND TIMING VARIABLES ---
unsigned long lastSecondPrint = 0;
unsigned long last10SecPrint = 0;
unsigned long lastHealthCheck = 0;
bool isAudioPlaying = false;
unsigned long audioStartTime = 0;
const char* currentPlayingFile = "";
unsigned long lastMinutePrayed = 0;

// --- PROFESSIONAL LOGGING FUNCTION ---
void sysLog(int level, const char* tag, const char* message) {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) return;
    
    const char* levelStr;
    switch(level) {
        case LOG_ERROR:   levelStr = "ERROR  "; break;
        case LOG_WARN:    levelStr = "WARN   "; break;
        case LOG_INFO:    levelStr = "INFO   "; break;
        case LOG_DEBUG:   levelStr = "DEBUG  "; break;
        default:          levelStr = "UNKNOW "; break;
    }
    
    Serial.printf("[%02d:%02d:%02d][%s][%s]: %s\n", 
                  timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec, 
                  levelStr, tag, message);
}

void sysLogf(int level, const char* tag, const char* fmt, ...) {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) return;
    
    const char* levelStr;
    switch(level) {
        case LOG_ERROR:   levelStr = "ERROR  "; break;
        case LOG_WARN:    levelStr = "WARN   "; break;
        case LOG_INFO:    levelStr = "INFO   "; break;
        case LOG_DEBUG:   levelStr = "DEBUG  "; break;
        default:          levelStr = "UNKNOW "; break;
    }
    
    char buffer[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    
    Serial.printf("[%02d:%02d:%02d][%s][%s]: %s\n", 
                  timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec, 
                  levelStr, tag, buffer);
}

// --- УЕБ ИНТЕРФЕЙС ---
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>🕌 Азан Система</title>
    <style>
        body { font-family: Arial, sans-serif; background: #f4f7f6; display: flex; flex-direction: column; align-items: center; padding: 20px; margin: 0; }
        .card { background: white; padding: 25px; border-radius: 12px; box-shadow: 0 4px 15px rgba(0,0,0,0.1); text-align: center; max-width: 500px; width: 100%; margin-bottom: 20px; }
        h2 { color: #2c3e50; margin-bottom: 15px; font-size: 20px; }
        .file-input { margin: 15px 0; padding: 10px; border: 2px dashed #3498db; border-radius: 8px; cursor: pointer; background: #ebf5fb; width: 85%; }
        .btn { color: white; border: none; padding: 12px 20px; font-size: 16px; border-radius: 8px; cursor: pointer; width: 100%; transition: 0.3s; font-weight: bold; margin-bottom: 10px; }
        .btn-green { background: #2ecc71; } .btn-green:hover { background: #27ae60; }
        .btn-red { background: #e74c3c; } .btn-red:hover { background: #c0392b; }
        .btn-orange { background: #e67e22; } .btn-orange:hover { background: #d35400; }
        .btn-blue { background: #3498db; } .btn-blue:hover { background: #2980b9; }
        .btn-small { padding: 6px 10px; font-size: 12px; margin: 2px; }
        .status-text { font-size: 16px; font-weight: bold; color: #34495e; margin: 10px 0; background: #f8f9fa; padding: 8px; border-radius: 6px; }
        .file-list { text-align: left; background: #f8f9fa; padding: 10px; border-radius: 8px; max-height: 300px; overflow-y: auto; }
        .file-item { display: flex; justify-content: space-between; align-items: center; padding: 8px; background: white; margin: 5px 0; border-radius: 4px; border-left: 4px solid #3498db; }
        .file-info { text-align: left; flex: 1; }
        .file-name { font-weight: bold; color: #2c3e50; }
        .file-size { font-size: 12px; color: #7f8c8d; }
        .btn-delete { background: #e74c3c; color: white; padding: 4px 8px; font-size: 12px; border: none; border-radius: 3px; cursor: pointer; }
        .btn-delete:hover { background: #c0392b; }
        .loading { color: #7f8c8d; font-style: italic; }
        .error { color: #e74c3c; }
        .success { color: #2ecc71; }
    </style>
</head>
<body>
    <div class="card">
        <h2>🎛️ Контрол в реално време</h2>
        <button class="btn btn-red" onclick="sendCmd('/stop')">🛑 Спри Азана</button>
        <div class="status-text">Нощна Аларма (Пре-Фаджр): <span id="pf_status">...</span></div>
        <button class="btn btn-orange" onclick="sendCmd('/toggle-prefajr')">⏰ Превключи Пре-Фаджр</button>
        <div class="status-text">Текущ Азан: <span id="azan_status">...</span></div>
        <button class="btn btn-blue" onclick="sendCmd('/next-azan')">🎵 Смени Азан Файл</button>
    </div>
    <div class="card">
        <h2>🕌 Качване на Нови Файлове</h2>
        <form method="POST" action="/upload" enctype="multipart/form-data" id="upload_form">
            <input type="file" name="update" class="file-input" accept=".wav,.mp3,.bin" required><br>
            <button type="submit" class="btn btn-green">Качи в SD Картата</button>
        </form>
        <div id="prg" style="font-weight:bold; color:#7f8c8d; margin-top:10px;"></div>
    </div>
    <div class="card">
        <h2>📁 Управление на Файлове</h2>
        <button class="btn btn-blue" onclick="loadFiles()">🔄 Освежи Списък</button>
        <div id="file_list" class="file-list">
            <div class="loading">Зарежда се списък на файлове...</div>
        </div>
    </div>
    <script>
        document.getElementById('upload_form').onsubmit = function() {
            document.getElementById('prg').innerHTML = "Качване... Моля изчакайте.";
            setTimeout(() => { loadFiles(); }, 2000);
            return true;
        };
        
        function sendCmd(url) { 
            fetch(url).then(res => res.text()).then(() => refreshStatus()); 
        }
        
        function loadFiles() {
            fetch('/list-files-api').then(res => res.json()).then(data => {
                let html = '';
                if (data.files && data.files.length > 0) {
                    data.files.forEach(file => {
                        const sizeKB = (file.size / 1024).toFixed(2);
                        html += `
                            <div class="file-item">
                                <div class="file-info">
                                    <div class="file-name">📄 ${file.name}</div>
                                    <div class="file-size">${sizeKB} KB</div>
                                </div>
                                <button class="btn-delete" onclick="deleteFile('${file.name}')">🗑️ Изтрий</button>
                            </div>
                        `;
                    });
                } else {
                    html = '<div class="loading">Няма файлове на SD картата</div>';
                }
                document.getElementById('file_list').innerHTML = html;
            }).catch(err => {
                document.getElementById('file_list').innerHTML = '<div class="error">Грешка при зареждане на файлове</div>';
            });
        }
        
        function deleteFile(filename) {
            if (confirm('Сигурен ли си, че искаш да изтриеш ' + filename + '?')) {
                fetch('/delete-file?name=' + encodeURIComponent(filename))
                    .then(res => res.json())
                    .then(data => {
                        if (data.success) {
                            alert('Файлът е изтрит успешно!');
                            loadFiles();
                        } else {
                            alert('Грешка: ' + data.error);
                        }
                    })
                    .catch(err => alert('Грешка при изтриване'));
            }
        }
        
        function refreshStatus() {
            fetch('/status-api').then(res => res.json()).then(data => {
                document.getElementById('pf_status').innerText = data.preFajr ? "ВКЛ" : "ИЗКЛ";
                document.getElementById('pf_status').style.color = data.preFajr ? "#2ecc71" : "#e74c3c";
                document.getElementById('azan_status').innerText = data.currentAzan;
            });
        }
        
        window.onload = function() {
            refreshStatus();
            loadFiles();
        };
        setInterval(refreshStatus, 4000);
    </script>
</body>
</html>
)rawliteral";

void audioTask(void *pvParameters);
void syncTime();
void checkAndPlayAzan();
void handleDebugConsole();
void printStatus();
bool getDayRecordFromBin(int day, DayRecord &record);
String minutesToTime(int totalMinutes);
void printHealthStatus();
int getTimeToNextPrayer();
bool fileExists(const char* path);
uint32_t getFileSize(const char* path);
void setupAudioCallbacks();
void playAudioFile(const char* filePath);
bool deleteFile(const char* path);

// --- SAFE WIFI TOGGLE FUNCTION ---
void toggleWiFi() {
    if (wifiIsOn) {
        sysLog(LOG_INFO, "WIFI", "Turning OFF (freeing memory and energy)...");
        server.end();
        WiFi.disconnect(); // Don't forget router cache - faster reconnect
        delay(150);
        WiFi.mode(WIFI_OFF);
        wifiIsOn = false;
        sysLog(LOG_INFO, "WIFI", "Wi-Fi turned OFF");
    } else {
        sysLog(LOG_INFO, "WIFI", "Turning ON...");
        WiFi.mode(WIFI_STA);
        WiFi.setSleep(false); // Disable power saving for faster/more stable connection
        WiFi.begin(ssid, password);
        
        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 15) {
            delay(500);
            Serial.print(".");
            attempts++;
        }
        
        if (WiFi.status() == WL_CONNECTED) {
            sysLog(LOG_INFO, "WIFI", "Connected!");
            sysLogf(LOG_INFO, "WIFI", "Web interface: http://%s", WiFi.localIP().toString().c_str());
            server.begin();
            // Only sync time if WiFi is actually connected
            if (WiFi.status() == WL_CONNECTED) {
                syncTime();
            }
            wifiIsOn = true;
        } else {
            sysLog(LOG_WARN, "WIFI", "Connection failed. Staying OFF");
            WiFi.disconnect();
            WiFi.mode(WIFI_OFF);
            wifiIsOn = false;
        }
    }
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    sysLog(LOG_INFO, "SYSTEM", "=== STARTING AZAN SYSTEM (AUTO WI-FI OFF MODE) ===");

    pinMode(WIFI_BUTTON_PIN, INPUT_PULLUP);

    spiMutex = xSemaphoreCreateMutex();
    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);
    if (SD.begin(SD_CS)) {
        sdCardInitialized = true;
        sysLog(LOG_INFO, "SDCARD", "Initialization successful");
    } else {
        sysLog(LOG_ERROR, "SDCARD", "Initialization failed!");
    }

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false); // Disable power saving for faster/stable connection
    WiFi.begin(ssid, password);
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 15) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        sysLog(LOG_INFO, "WIFI", "Connected successfully");
        sysLogf(LOG_INFO, "WIFI", "Web interface: http://%s", WiFi.localIP().toString().c_str());
        // Only sync time if WiFi is actually connected
        syncTime();
    } else {
        sysLog(LOG_WARN, "WIFI", "Connection failed. Running in offline mode");
        wifiIsOn = false;
    }

    audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
    audio.setVolume(15);
    sysLogf(LOG_INFO, "AUDIO", "I2S initialized - BCLK:%d, LRC:%d, DOUT:%d | Volume: 15", 
           I2S_BCLK, I2S_LRC, I2S_DOUT);
 

    // Регистрираме рутовете само веднъж тук
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){ request->send_P(200, "text/html", index_html); });
    server.on("/stop", HTTP_GET, [](AsyncWebServerRequest *request){
        audio.stopSong();
        sysLog(LOG_INFO, "WEB", "STOP command received");
        isAudioPlaying = false;
        request->send(200, "text/plain", "Stopped");
    });
    server.on("/toggle-prefajr", HTTP_GET, [](AsyncWebServerRequest *request){
        preFajrEnabled = !preFajrEnabled;
        sysLogf(LOG_INFO, "WEB", "Pre-Fajr toggled: %s", preFajrEnabled ? "ON" : "OFF");
        request->send(200, "text/plain", preFajrEnabled ? "ON" : "OFF");
    });
    server.on("/next-azan", HTTP_GET, [](AsyncWebServerRequest *request){
        currentAzanIndex = (currentAzanIndex + 1) % numAzanFiles;
        sysLogf(LOG_INFO, "WEB", "Azan file changed: %s", azanFiles[currentAzanIndex]);
        request->send(200, "text/plain", azanFiles[currentAzanIndex]);
    });
    server.on("/status-api", HTTP_GET, [](AsyncWebServerRequest *request){
        String json = "{\"preFajr\":" + String(preFajrEnabled ? "true" : "false") + ",\"currentAzan\":\"" + String(azanFiles[currentAzanIndex]) + "\"}";
        request->send(200, "application/json", json);
    });
    server.on("/upload", HTTP_POST, [](AsyncWebServerRequest *request){
        request->send(200, "text/html", "<h3>✅ File uploaded successfully!</h3><a href='/'>Back</a>");
    }, [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final){
        static File file;
        if(!index){
            file = SD.open("/" + filename, FILE_WRITE);
            sysLogf(LOG_DEBUG, "UPLOAD", "Starting upload: %s", filename.c_str());
        }
        if(file && len > 0) {
            // Lock ONLY for the actual write operation, then release immediately
            if (spiMutex != NULL) xSemaphoreTake(spiMutex, portMAX_DELAY);
            file.write(data, len);
            if (spiMutex != NULL) xSemaphoreGive(spiMutex);
        }
        if(final){
            if(file) {
                if (spiMutex != NULL) xSemaphoreTake(spiMutex, portMAX_DELAY);
                file.close();
                if (spiMutex != NULL) xSemaphoreGive(spiMutex);
            }
            sysLogf(LOG_INFO, "UPLOAD", "File upload completed: %s (%lu bytes total)", filename.c_str(), index + len);
        }
    });
    
    // --- LIST FILES API ---
    server.on("/list-files-api", HTTP_GET, [](AsyncWebServerRequest *request){
        String json = "{\"files\":[";
        
        if (!sdCardInitialized) {
            json += "]}";
            request->send(200, "application/json", json);
            return;
        }
        
        if (spiMutex != NULL) xSemaphoreTake(spiMutex, portMAX_DELAY);
        File root = SD.open("/");
        if (!root) {
            if (spiMutex != NULL) xSemaphoreGive(spiMutex);
            json += "]}";
            request->send(200, "application/json", json);
            return;
        }
        
        bool first = true;
        File file = root.openNextFile();
        while(file) {
            if (!file.isDirectory()) {
                if (!first) json += ",";
                json += "{\"name\":\"" + String(file.name()) + "\",\"size\":" + String(file.size()) + "}";
                first = false;
            }
            file.close();
            file = root.openNextFile();
        }
        root.close();
        if (spiMutex != NULL) xSemaphoreGive(spiMutex);
        
        json += "]}";
        sysLogf(LOG_DEBUG, "WEB", "Listed files - JSON size: %d bytes", json.length());
        request->send(200, "application/json", json);
    });
    
    // --- DELETE FILE API ---
    server.on("/delete-file", HTTP_GET, [](AsyncWebServerRequest *request){
        if (!request->hasParam("name")) {
            request->send(400, "application/json", "{\"success\":false,\"error\":\"Missing filename\"}");
            return;
        }
        
        String filename = request->getParam("name")->value();
        sysLogf(LOG_INFO, "WEB", "Delete requested: %s", filename.c_str());
        
        if (deleteFile(filename.c_str())) {
            sysLogf(LOG_INFO, "WEB", "File deleted: %s", filename.c_str());
            request->send(200, "application/json", "{\"success\":true}");
        } else {
            sysLogf(LOG_ERROR, "WEB", "Failed to delete: %s", filename.c_str());
            request->send(200, "application/json", "{\"success\":false,\"error\":\"Failed to delete file\"}");
        }
    });

    if (wifiIsOn) server.begin();

    xTaskCreatePinnedToCore(audioTask, "AudioTask", 16384, NULL, 5, &AudioTaskHandle, 0);
    printStatus();
}

void loop() {
    handleDebugConsole(); 
    
    // --- HARDWARE BUTTON CHECK FOR WI-FI ---
    if (digitalRead(WIFI_BUTTON_PIN) == LOW) {
        if (millis() - lastButtonPress > debounceDelay) {
            lastButtonPress = millis();
            toggleWiFi();
        }
    }

    // --- AUTO WI-FI SHUTDOWN AFTER 5 MINUTES ---
    if (!autoWifiShutdownDone && millis() > AUTO_WIFI_OFF_MS) {
        sysLog(LOG_INFO, "SYSTEM", "Auto Wi-Fi shutdown triggered");
        if (wifiIsOn) {
            toggleWiFi();
        }
        autoWifiShutdownDone = true;
    }
    
    // --- AUTO WI-FI ON/OFF AFTER AZAN FINISHES (Non-blocking detection) ---
    bool currentAudioRunning = audio.isRunning();
    
    // Detect audio STOP (was playing, now stopped)
    if (wasAudioPlaying && !currentAudioRunning && isAudioPlaying) {
        sysLog(LOG_INFO, "SYSTEM", "Azan finished - Auto-enabling WiFi for 5 minutes");
        if (!wifiIsOn) {
            toggleWiFi();
        }
        postPrayerWifiActive = true;
        postPrayerWifiTimer = millis(); // Record time when we enabled WiFi
    }
    
    // Auto-disable WiFi after 5 minutes
    if (postPrayerWifiActive && (millis() - postPrayerWifiTimer >= WIFI_AUTO_DURATION_AFTER_AZAN)) {
        sysLog(LOG_INFO, "SYSTEM", "5-minute WiFi timeout after Azan - Auto-disabling");
        if (wifiIsOn) {
            toggleWiFi();
        }
        postPrayerWifiActive = false;
        postPrayerWifiTimer = 0;
    }
    
    // Update audio state for next iteration
    wasAudioPlaying = currentAudioRunning;
    
    unsigned long currentMs = millis();
    
    // --- PRINT CURRENT TIME EVERY SECOND ---
    if (currentMs - lastSecondPrint >= 1000) {
        lastSecondPrint = currentMs;
        struct tm timeinfo;
        if (getLocalTime(&timeinfo)) {
            bool audioRunning = audio.isRunning();
            sysLogf(LOG_DEBUG, "TIME", "Current: %02d:%02d:%02d | Audio: %s (State: %s)", 
                   timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec,
                   isAudioPlaying ? "PLAYING" : "IDLE",
                   audioRunning ? "RUNNING" : "STOPPED");
        }
    }
    
    // --- PRINT HEALTH STATUS AND TIME TO NEXT PRAYER EVERY 10 SECONDS ---
    if (currentMs - last10SecPrint >= 10000) {
        last10SecPrint = currentMs;
        printHealthStatus();
        int timeToNext = getTimeToNextPrayer();
        if (timeToNext >= 0) {
            sysLogf(LOG_INFO, "PRAYER", "Time to next prayer: %d minutes", timeToNext);
        }
    }
    
    static unsigned long lastCheckTime = 0;
    if (millis() - lastCheckTime > 4000) {
        lastCheckTime = millis();
        checkAndPlayAzan();    
    }
    yield();
}

void audioTask(void *pvParameters) {
    sysLog(LOG_DEBUG, "AUDIO", "Audio task started on core 0 with high priority");
    uint32_t loopCounter = 0;
    while(1) {
        audio.loop();
        loopCounter++;
        
        // Log audio stats every 5000 loops (roughly every 5 seconds)
        if (loopCounter % 5000 == 0 && isAudioPlaying) {
            sysLogf(LOG_DEBUG, "AUDIO_TASK", "Audio loop running - isRunning: %d, loopCount: %lu",
                   audio.isRunning(), loopCounter);
        }
        
        // Minimal delay to allow other tasks to run
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

void syncTime() {
    sysLog(LOG_DEBUG, "NTP", "Syncing time from NTP server...");
    configTime(2 * 3600, 3600, ntpServer);
    
    // Wait for time to be set
    time_t now = time(nullptr);
    struct tm timeinfo = *localtime(&now);
    int attempts = 0;
    while (timeinfo.tm_year < (2023 - 1900) && attempts < 20) {
        delay(500);
        now = time(nullptr);
        timeinfo = *localtime(&now);
        attempts++;
    }
    
    if (timeinfo.tm_year >= (2023 - 1900)) {
        sysLogf(LOG_INFO, "NTP", "✓ Time synced successfully: %04d-%02d-%02d %02d:%02d:%02d", 
               timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
               timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    } else {
        sysLogf(LOG_WARN, "NTP", "✗ Time sync failed (year still %d, tried %d times)", 
               timeinfo.tm_year + 1900, attempts);
    }
}

void checkAndPlayAzan() {
    static int lastCheckedMinute = -1;
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) return;

    time_t rawtime = mktime(&timeinfo);
    rawtime += (timeOffsetMinutes * 60);
    struct tm *adjustedTime = localtime(&rawtime);

    int day = adjustedTime->tm_yday + 1;
    if (adjustedTime->tm_min == lastCheckedMinute) return;
    lastCheckedMinute = adjustedTime->tm_min;

    int currentTotalMinutes = adjustedTime->tm_hour * 60 + adjustedTime->tm_min;

    // --- CACHE PRAYER TIMES (only read from SD once per day) ---
    if (cachedPrayerDay != day || !cachedPrayerTimesValid) {
        if (!getDayRecordFromBin(day, cachedPrayerTimes)) {
            sysLog(LOG_WARN, "PRAYER", "Could not read prayer times from binary file");
            cachedPrayerTimesValid = false;
            return;
        }
        cachedPrayerDay = day;
        cachedPrayerTimesValid = true;
        sysLogf(LOG_DEBUG, "PRAYER", "✓ Prayer times cached for day %d (Fajr: %s)", 
               day, minutesToTime(cachedPrayerTimes.times[0]).c_str());
    }

    int fajrMinutes = cachedPrayerTimes.times[0];
    int preFajrTarget = fajrMinutes - 30;

    // --- PRE-FAJR ALARM CHECK ---
    if (preFajrEnabled && currentTotalMinutes == preFajrTarget && lastPreFajrDay != day) {
        sysLogf(LOG_INFO, "PRAYER", "PRE-FAJR ALARM triggered (30 min before Fajr at %s)", 
               minutesToTime(fajrMinutes).c_str());
        playAudioFile(azanFiles[currentAzanIndex]);
        isAudioPlaying = true;
        audioStartTime = millis();
        currentPlayingFile = azanFiles[currentAzanIndex];
        lastPreFajrDay = day;
        lastMinutePrayed = currentTotalMinutes;
        return; 
    }

    const char* prayerNames[] = {"FAJR", "DUHA", "DHUHR", "ASR", "MAGHRIB", "ISHA"};
    
    // --- REGULAR PRAYER TIMES CHECK (skip DUHA at index 1 - it's informational only) ---
    for (int i = 0; i < 6; i++) {
        // Skip DUHA (index 1) - it's only when Fajr ends, not a prayer time to alarm
        if (i == 1) continue;
        
        if (currentTotalMinutes == cachedPrayerTimes.times[i]) {
            sysLogf(LOG_INFO, "PRAYER", "Prayer time started: %s (%s)", 
                   prayerNames[i], minutesToTime(cachedPrayerTimes.times[i]).c_str());
            
            playAudioFile(azanFiles[currentAzanIndex]);
            
            isAudioPlaying = true;
            audioStartTime = millis();
            currentPlayingFile = azanFiles[currentAzanIndex];
            lastMinutePrayed = currentTotalMinutes;
            break;
        }
    }
    
    // --- CHECK IF AUDIO FINISHED PLAYING ---
    bool currentAudioRunningState = audio.isRunning();
    if (isAudioPlaying && lastAudioRunningState && !currentAudioRunningState) {
        isAudioPlaying = false;
        unsigned long duration = millis() - audioStartTime;
        sysLogf(LOG_INFO, "AUDIO", "Azan finished playing. Duration: %lu ms. File: %s", 
               duration, currentPlayingFile);
    }
    lastAudioRunningState = currentAudioRunningState;
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
            sysLog(LOG_INFO, "DEBUG", "Time offset reset to 0");
            printStatus(); 
        }
        else if (input.startsWith("+") || input.startsWith("-")) {
            int oldOffset = timeOffsetMinutes;
            timeOffsetMinutes += input.toInt();
            sysLogf(LOG_DEBUG, "DEBUG", "Time offset changed: %d -> %d minutes", oldOffset, timeOffsetMinutes);
            printStatus();
        }
        else if (input == "LISTFILES") {
            sysLog(LOG_INFO, "DEBUG", "Listing audio files and their status:");
            for (int i = 0; i < numAzanFiles; i++) {
                bool exists = fileExists(azanFiles[i]);
                uint32_t size = exists ? getFileSize(azanFiles[i]) : 0;
                sysLogf(LOG_INFO, "DEBUG", "  [%d] %s -> Exists: %s | Size: %lu bytes", 
                       i, azanFiles[i], exists ? "YES" : "NO", size);
            }
        }
        else if (input == "SDLS") {
            sysLog(LOG_INFO, "DEBUG", "=== All files on SD card root: ===");
            if (!sdCardInitialized) {
                sysLog(LOG_ERROR, "DEBUG", "SD Card not initialized!");
                return;
            }
            
            if (spiMutex != NULL) xSemaphoreTake(spiMutex, portMAX_DELAY);
            File root = SD.open("/");
            if (!root) {
                sysLog(LOG_ERROR, "DEBUG", "Cannot open root directory");
                if (spiMutex != NULL) xSemaphoreGive(spiMutex);
                return;
            }
            
            File file = root.openNextFile();
            int count = 0;
            while(file) {
                if (file.isDirectory()) {
                    sysLogf(LOG_INFO, "DEBUG", "  [DIR] %s/", file.name());
                } else {
                    sysLogf(LOG_INFO, "DEBUG", "  [FILE] %s (%lu bytes)", file.name(), file.size());
                    count++;
                }
                file.close();
                file = root.openNextFile();
            }
            root.close();
            if (spiMutex != NULL) xSemaphoreGive(spiMutex);
            sysLogf(LOG_INFO, "DEBUG", "Total files: %d", count);
        }
        else if (input.startsWith("PLAYTEST:")) {
            String filename = input.substring(9);
            filename.trim();
            sysLogf(LOG_INFO, "DEBUG", "Testing audio playback: %s", filename.c_str());
            playAudioFile(filename.c_str());
        }
        else if (input == "AUDIOINFO") {
            sysLogf(LOG_INFO, "DEBUG", "Audio Info: isRunning=%d | isPlaying=%d | Volume=%d",
                   audio.isRunning(), isAudioPlaying, 15);
        }
        else {
            sysLogf(LOG_WARN, "DEBUG", "Unknown command: '%s'", input.c_str());
            sysLog(LOG_INFO, "DEBUG", "Available commands:");
            sysLog(LOG_INFO, "DEBUG", "  STATUS - Show system status");
            sysLog(LOG_INFO, "DEBUG", "  OFFSET:0 - Reset time offset");
            sysLog(LOG_INFO, "DEBUG", "  +N/-N - Adjust time offset");
            sysLog(LOG_INFO, "DEBUG", "  LISTFILES - List configured audio files");
            sysLog(LOG_INFO, "DEBUG", "  SDLS - List all files on SD card");
            sysLog(LOG_INFO, "DEBUG", "  PLAYTEST:filename - Test audio playback");
            sysLog(LOG_INFO, "DEBUG", "  AUDIOINFO - Show audio library status");
        }
    }
}

void printStatus() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        sysLog(LOG_ERROR, "STATUS", "Failed to get time from system");
        return;
    }

    time_t rawtime = mktime(&timeinfo);
    rawtime += (timeOffsetMinutes * 60);
    struct tm *adj = localtime(&rawtime);
    int day = adj->tm_yday + 1;

    sysLog(LOG_INFO, "STATUS", "========== SYSTEM STATUS ==========");
    sysLogf(LOG_INFO, "STATUS", "Real time: %02d:%02d:%02d | Time offset: %d min", 
           timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec, timeOffsetMinutes);
    sysLogf(LOG_INFO, "STATUS", "Virtual time: %02d:%02d:%02d | Day of year: %d", 
           adj->tm_hour, adj->tm_min, adj->tm_sec, day);
    sysLogf(LOG_INFO, "STATUS", "Wi-Fi: %s | SD Card: %s | Audio: %s", 
           wifiIsOn ? "ON" : "OFF", 
           sdCardInitialized ? "OK" : "ERROR", 
           isAudioPlaying ? "PLAYING" : "IDLE");
    sysLogf(LOG_INFO, "STATUS", "Pre-Fajr: %s | Current Azan: %s", 
           preFajrEnabled ? "ENABLED" : "DISABLED",
           azanFiles[currentAzanIndex]);
    sysLog(LOG_INFO, "STATUS", "===============================");
}

bool getDayRecordFromBin(int day, DayRecord &record) {
    if (!sdCardInitialized) return false;
    if (spiMutex != NULL) xSemaphoreTake(spiMutex, portMAX_DELAY);

    File in = SD.open(DEFAULT_PRAYER_TIMES_FILE, FILE_READ);
    if (!in) {
        if (spiMutex != NULL) xSemaphoreGive(spiMutex);
        return false;
    }

    if (!in.seek((day - 1) * BYTES_PER_DAY, SeekSet)) {
        in.close();
        if (spiMutex != NULL) xSemaphoreGive(spiMutex);
        return false;
    }

    size_t bytesRead = in.read((uint8_t*)&record, sizeof(DayRecord));
    in.close();
    
    if (spiMutex != NULL) xSemaphoreGive(spiMutex);
    return (bytesRead == sizeof(DayRecord));
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
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        sysLog(LOG_WARN, "HEALTH", "Failed to get time");
        return;
    }

    // --- MEMORY USAGE ---
    uint32_t freeHeap = ESP.getFreeHeap();
    uint32_t totalHeap = ESP.getHeapSize();
    uint8_t heapUsagePercent = (100 * (totalHeap - freeHeap)) / totalHeap;
    
    // --- WiFi RSSI if connected ---
    int rssi = -120;
    const char* wifiStatus = "OFF";
    if (wifiIsOn && WiFi.status() == WL_CONNECTED) {
        rssi = WiFi.RSSI();
        wifiStatus = "CONNECTED";
    } else if (wifiIsOn) {
        wifiStatus = "CONNECTING";
    }
    
    // --- Audio Status ---
    bool audioRunning = audio.isRunning();
    const char* audioStatus = isAudioPlaying ? (audioRunning ? "PLAYING" : "STOPPING") : "IDLE";
    
    // --- Build comprehensive health message ---
    sysLogf(LOG_INFO, "HEALTH", 
           "Heap: %lu/%lu (%u%%) | WiFi: %s (RSSI:%d) | Audio: %s (Running:%s) | SD: %s | File: %s",
           freeHeap, totalHeap, heapUsagePercent, 
           wifiStatus, rssi, audioStatus,
           audioRunning ? "YES" : "NO",
           sdCardInitialized ? "OK" : "FAIL",
           currentPlayingFile);
}

// --- GET TIME TO NEXT PRAYER ---
int getTimeToNextPrayer() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) return -1;

    time_t rawtime = mktime(&timeinfo);
    rawtime += (timeOffsetMinutes * 60);
    struct tm *adjustedTime = localtime(&rawtime);

    int day = adjustedTime->tm_yday + 1;
    int currentTotalMinutes = adjustedTime->tm_hour * 60 + adjustedTime->tm_min;

    // Use cached prayer times if available for today, otherwise read from file
    DayRecord todayTimes;
    if (cachedPrayerDay == day && cachedPrayerTimesValid) {
        todayTimes = cachedPrayerTimes;
    } else {
        if (!getDayRecordFromBin(day, todayTimes)) return -1;
    }

    // Find next prayer (skip DUHA at index 1)
    for (int i = 0; i < 6; i++) {
        if (i == 1) continue; // Skip DUHA
        if (todayTimes.times[i] > currentTotalMinutes) {
            return (todayTimes.times[i] - currentTotalMinutes);
        }
    }

    // If no prayer found today, check tomorrow
    if (getDayRecordFromBin(day + 1, todayTimes)) {
        return ((1440 - currentTotalMinutes) + todayTimes.times[0]);
    }

    return -1;
}

// --- AUDIO FILE DIAGNOSTICS ---
bool fileExists(const char* path) {
    if (!sdCardInitialized) {
        sysLog(LOG_WARN, "FILEIO", "SD Card not initialized");
        return false;
    }
    
    if (spiMutex != NULL) xSemaphoreTake(spiMutex, portMAX_DELAY);
    File file = SD.open(path, FILE_READ);
    bool exists = file ? true : false;
    if (file) file.close();
    if (spiMutex != NULL) xSemaphoreGive(spiMutex);
    
    sysLogf(LOG_DEBUG, "FILEIO", "File exists check: %s -> %s", path, exists ? "YES" : "NO");
    return exists;
}

uint32_t getFileSize(const char* path) {
    if (!sdCardInitialized) return 0;
    
    if (spiMutex != NULL) xSemaphoreTake(spiMutex, portMAX_DELAY);
    File file = SD.open(path, FILE_READ);
    uint32_t size = file ? file.size() : 0;
    if (file) file.close();
    if (spiMutex != NULL) xSemaphoreGive(spiMutex);
    
    sysLogf(LOG_DEBUG, "FILEIO", "File size: %s -> %lu bytes", path, size);
    return size;
}

// --- DELETE FILE FROM SD CARD ---
bool deleteFile(const char* path) {
    if (!sdCardInitialized) {
        sysLog(LOG_WARN, "FILEIO", "SD Card not initialized");
        return false;
    }
    
    if (!path || strlen(path) == 0) {
        sysLog(LOG_ERROR, "FILEIO", "Invalid file path for deletion");
        return false;
    }
    
    // Add leading slash if not present
    String fullPath = path;
    if (!fullPath.startsWith("/")) {
        fullPath = "/" + fullPath;
    }
    
    sysLogf(LOG_DEBUG, "FILEIO", "Attempting to delete: %s", fullPath.c_str());
    
    if (spiMutex != NULL) xSemaphoreTake(spiMutex, portMAX_DELAY);
    bool result = SD.remove(fullPath.c_str());
    if (spiMutex != NULL) xSemaphoreGive(spiMutex);
    
    if (result) {
        sysLogf(LOG_INFO, "FILEIO", "File deleted successfully: %s", fullPath.c_str());
    } else {
        sysLogf(LOG_WARN, "FILEIO", "Failed to delete file: %s", fullPath.c_str());
    }
    
    return result;
}

// --- WRAPPER TO PLAY AUDIO WITH DETAILED LOGGING ---
void playAudioFile(const char* filePath) {
    if (!filePath || strlen(filePath) == 0) {
        sysLog(LOG_ERROR, "AUDIO", "Invalid file path");
        isAudioPlaying = false;
        return;
    }
    
    sysLogf(LOG_DEBUG, "AUDIO", "Attempting to play: %s", filePath);
    
    // Check file existence
    if (!fileExists(filePath)) {
        sysLogf(LOG_ERROR, "AUDIO", "File not found: %s", filePath);
        isAudioPlaying = false;
        return;
    }
    
    uint32_t fileSize = getFileSize(filePath);
    if (fileSize == 0) {
        sysLogf(LOG_ERROR, "AUDIO", "File is empty: %s", filePath);
        isAudioPlaying = false;
        return;
    }
    
    sysLogf(LOG_INFO, "AUDIO", "File valid. Size: %lu bytes. Connecting...", fileSize);
    
    if (spiMutex != NULL) xSemaphoreTake(spiMutex, portMAX_DELAY);
    bool result = audio.connecttoFS(SD, filePath);
    if (spiMutex != NULL) xSemaphoreGive(spiMutex);
    
    if (result) {
        sysLogf(LOG_INFO, "AUDIO", "Connected to file: %s", filePath);
    } else {
        sysLogf(LOG_ERROR, "AUDIO", "Failed to connect to file: %s", filePath);
        isAudioPlaying = false;
    }
}