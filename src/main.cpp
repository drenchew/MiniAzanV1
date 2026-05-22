#include "Arduino.h"
#include "WiFi.h"
#include "time.h"
#include <sys/time.h>
#include "AsyncTCP.h"
#include "ESPAsyncWebServer.h"
#include <nvs_flash.h>
#include <nvs.h>
#include <Wire.h>
#include "SpiArchitecture.h"
#include "StorageManager.h"
#include "TimeManager.h"
#include "AudioManager.h"

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
#define I2S_DOUT       27  // <--- Changed to 27 to free up 22
#define WIFI_BUTTON_PIN 4

#define DEFAULT_VOLUME 9

#define DEFAULT_PRAYER_TIMES_FILE "/prayer_times.bin"
#define BYTES_PER_DAY 12

struct DayRecord {
    uint16_t times[6]; 
};

TimeManager timeMgr;
StorageManager storageMgr;
AudioManager audioMgr;

// Глобални обекти
AsyncWebServer server(80);

int timeOffsetMinutes = 0; 
bool preFajrEnabled = false; 

const char* azanFiles[] = {"/Luhaidan_Azan_1.mp3", "/Bahanan_Azan_1.mp3 ", "/azan3.mp3"};
const int numAzanFiles = 3;
int currentAzanIndex = 0; 
int lastPreFajrDay = -1; 
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

// --- VOLUME CONTROL AND NVS ---
uint8_t currentVolume = 15;
const uint8_t MIN_VOLUME = 0;
const uint8_t MAX_VOLUME = 21;
const char* NVS_NAMESPACE = "azan_system";
const char* NVS_VOLUME_KEY = "volume";

// --- WiFi AUTO-ON BEFORE PRAYER ---
unsigned long wifiAutoOnTime = 0;
bool wifiAutoOnPending = false;
int wifiConnectRetries = 0;
const int MAX_WIFI_RETRIES = 5;
const unsigned long WIFI_ON_BEFORE_PRAYER = 2 * 60000UL; // Turn on 2 minutes before prayer
unsigned long nextPrayerTime = 0;

// --- AUTO WIFI OFF CONTROL ---
bool autoWifiOffEnabled = true;

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

void moduleLog(int level, const char* tag, const char* message) {
    sysLog(level, tag, message);
}

static void debugSdLogLine(const char* line) {
    sysLog(LOG_INFO, "DEBUG", line);
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
        <h2>🔊 Сила на Звука</h2>
        <div class="status-text">Ниво: <span id="volume_display">50</span>%</div>
        <input 
            type="range" 
            id="volume_slider"
            min="0" 
            max="100" 
            value="50"
            style="width: 100%; cursor: pointer; height: 6px;"
            oninput="updateVolume(this.value)"
        >
    </div>

    <div class="card">
        <h2>⚙️ Настройки WiFi</h2>
        <div class="status-text">Автоматично Изключване: <span id="auto_off_status">ВКЛ</span></div>
        <button class="btn btn-blue" onclick="sendCmd('/toggle-auto-wifi-off')">🔄 Превключи</button>
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
                
                // Update volume slider and display
                const vol = Math.round((data.volume / 21) * 100);
                document.getElementById('volume_slider').value = vol;
                document.getElementById('volume_display').innerText = vol;
                
                // Update auto WiFi off status
                document.getElementById('auto_off_status').innerText = data.autoWifiOffEnabled ? "ВКЛ" : "ИЗКЛ";
                document.getElementById('auto_off_status').style.color = data.autoWifiOffEnabled ? "#2ecc71" : "#e74c3c";
            });
        }
        
        function updateVolume(percent) {
            const volume = Math.round((percent / 100) * 21);
            document.getElementById('volume_display').innerText = percent;
            fetch('/set-volume?vol=' + volume).then(res => res.text());
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

void checkAndPlayAzan();
void handleDebugConsole();
void printStatus();
bool getDayRecordFromBin(int day, DayRecord &record);
String minutesToTime(int totalMinutes);
void printHealthStatus();
int getTimeToNextPrayer();
void playAudioFile(const char* filePath);
static void debugSdLogLine(const char* line);

// --- SAFE WIFI TOGGLE FUNCTION ---
void toggleWiFi() {
    if (wifiIsOn) {
        sysLog(LOG_INFO, "WIFI", "Turning OFF (freeing memory and energy)...");
        server.end();
        WiFi.disconnect(); // Don't forget router cache - faster reconnect
        delay(150);
        WiFi.mode(WIFI_OFF);
        wifiIsOn = false;
        timeMgr.setWifiConnected(false);
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
            timeMgr.setWifiConnected(true);
            timeMgr.requestNtpSync();
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

    if (!storageMgr.begin({}, moduleLog)) {
        sysLog(LOG_ERROR, "STORAGE", "VSPI SD init failed");
    }

    Wire.begin();

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false); // Disable power saving for faster/stable connection
    WiFi.begin(ssid, password);
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 15) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    
    bool wifiConnected = (WiFi.status() == WL_CONNECTED);
    if (wifiConnected) {
        sysLog(LOG_INFO, "WIFI", "Connected successfully");
        sysLogf(LOG_INFO, "WIFI", "Web interface: http://%s", WiFi.localIP().toString().c_str());
        wifiIsOn = true;
    } else {
        sysLog(LOG_WARN, "WIFI", "Connection failed. Running in offline mode");
        wifiIsOn = false;
    }

    // --- INITIALIZE NVS FOR PERSISTENT STORAGE ---
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    currentVolume = loadVolumeFromNVS();

    TimeManager::Config tmCfg;
    tmCfg.ntpServer = ntpServer;
    timeMgr.begin(tmCfg, moduleLog);
    timeMgr.setWifiConnected(wifiConnected);
    if (wifiConnected && !timeMgr.rtcUsable()) {
        timeMgr.requestNtpSync();
    }

    AudioManager::Config audioCfg;
    audioCfg.pins = {I2S_BCLK, I2S_LRC, I2S_DOUT};
    audioCfg.defaultVolume = currentVolume;
    audioMgr.begin(storageMgr, audioCfg, moduleLog);
    audioMgr.setVolume(currentVolume);
 

    // Регистрираме рутовете само веднъж тук
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){ request->send_P(200, "text/html", index_html); });
    server.on("/stop", HTTP_GET, [](AsyncWebServerRequest *request){
        audioMgr.stop();
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
        String json = "{\"preFajr\":" + String(preFajrEnabled ? "true" : "false") 
                    + ",\"currentAzan\":\"" + String(azanFiles[currentAzanIndex]) 
                    + "\",\"volume\":" + String(currentVolume)
                    + ",\"autoWifiOffEnabled\":" + String(autoWifiOffEnabled ? "true" : "false")
                    + "}";
        request->send(200, "application/json", json);
    });
    server.on("/set-volume", HTTP_GET, [](AsyncWebServerRequest *request){
        if (request->hasParam("vol")) {
            uint8_t newVolume = atoi(request->getParam("vol")->value().c_str());
            if (newVolume >= MIN_VOLUME && newVolume <= MAX_VOLUME) {
                currentVolume = newVolume;
                audioMgr.setVolume(currentVolume);
                saveVolumeToNVS(currentVolume);
                sysLogf(LOG_INFO, "AUDIO", "Volume changed to: %d", currentVolume);
                request->send(200, "text/plain", "OK");
            } else {
                request->send(400, "text/plain", "Invalid volume");
            }
        } else {
            request->send(400, "text/plain", "Missing vol parameter");
        }
    });
    server.on("/toggle-auto-wifi-off", HTTP_GET, [](AsyncWebServerRequest *request){
        autoWifiOffEnabled = !autoWifiOffEnabled;
        sysLogf(LOG_INFO, "WEB", "Auto WiFi off toggled: %s", autoWifiOffEnabled ? "ON" : "OFF");
        request->send(200, "text/plain", autoWifiOffEnabled ? "ON" : "OFF");
    });
    server.on("/upload", HTTP_POST, [](AsyncWebServerRequest *request){
        request->send(200, "text/html", "<h3>✅ File uploaded successfully!</h3><a href='/'>Back</a>");
    }, [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final){
        if (!index) {
            if (!storageMgr.uploadBegin(filename.c_str())) {
                sysLogf(LOG_WARN, "UPLOAD", "uploadBegin failed: %s", filename.c_str());
            } else {
                sysLogf(LOG_DEBUG, "UPLOAD", "Starting upload: %s", filename.c_str());
            }
        }
        if (len > 0) {
            storageMgr.uploadWrite(data, len);
        }
        if (final) {
            storageMgr.uploadEnd(true);
            sysLogf(LOG_INFO, "UPLOAD", "File upload completed: %s (%lu bytes total)", filename.c_str(), index + len);
        }
    });
    
    // --- LIST FILES API ---
    server.on("/list-files-api", HTTP_GET, [](AsyncWebServerRequest *request){
        String json;
        storageMgr.listRootFilesJson(json);
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
        
        if (storageMgr.removeFile(filename.c_str())) {
            sysLogf(LOG_INFO, "WEB", "File deleted: %s", filename.c_str());
            request->send(200, "application/json", "{\"success\":true}");
        } else {
            sysLogf(LOG_ERROR, "WEB", "Failed to delete: %s", filename.c_str());
            request->send(200, "application/json", "{\"success\":false,\"error\":\"Failed to delete file\"}");
        }
    });

    if (wifiIsOn) server.begin();

    printStatus();
}

void loop() {
    timeMgr.update();
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
    
    // --- AUTO WI-FI ON BEFORE PRAYER (with retry logic) ---
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
        time_t rawtime = mktime(&timeinfo);
        rawtime += (timeOffsetMinutes * 60);
        struct tm *adjustedTime = localtime(&rawtime);
        int day = adjustedTime->tm_yday + 1;
        int currentTotalMinutes = adjustedTime->tm_hour * 60 + adjustedTime->tm_min;
        
        // Load prayer times if needed
        if (cachedPrayerDay != day || !cachedPrayerTimesValid) {
            if (getDayRecordFromBin(day, cachedPrayerTimes)) {
                cachedPrayerDay = day;
                cachedPrayerTimesValid = true;
            }
        }
        
        // Find the next prayer time (skip DUHA)
        if (cachedPrayerTimesValid) {
            int nextPrayer = -1;
            for (int i = 0; i < 6; i++) {
                if (i == 1) continue; // Skip DUHA
                if (cachedPrayerTimes.times[i] >= currentTotalMinutes) {
                    nextPrayer = cachedPrayerTimes.times[i];
                    break;
                }
            }
            
            // If no prayer found today, use first prayer tomorrow
            if (nextPrayer == -1 && getDayRecordFromBin(day + 1, cachedPrayerTimes)) {
                nextPrayer = cachedPrayerTimes.times[0];
            }
            
            if (nextPrayer != -1) {
                int minuteBeforePrayer = nextPrayer - (WIFI_ON_BEFORE_PRAYER / 60000);
                
                // Turn on WiFi 2 minutes before prayer
                if (currentTotalMinutes == minuteBeforePrayer && !wifiAutoOnPending) {
                    sysLogf(LOG_INFO, "SYSTEM", "Prayer in 2 minutes - Starting WiFi auto-on (retry up to %d times)", MAX_WIFI_RETRIES);
                    wifiAutoOnPending = true;
                    wifiConnectRetries = 0;
                    wifiAutoOnTime = millis();
                    if (!wifiIsOn) {
                        toggleWiFi();
                    }
                }
                
                // Retry WiFi connection if not connected
                if (wifiAutoOnPending && !wifiIsOn) {
                    if (millis() - wifiAutoOnTime > 10000 && wifiConnectRetries < MAX_WIFI_RETRIES) {
                        wifiConnectRetries++;
                        sysLogf(LOG_INFO, "SYSTEM", "WiFi retry %d/%d", wifiConnectRetries, MAX_WIFI_RETRIES);
                        wifiAutoOnTime = millis();
                        toggleWiFi();
                    } else if (wifiConnectRetries >= MAX_WIFI_RETRIES) {
                        sysLog(LOG_WARN, "SYSTEM", "WiFi auto-on failed after retries, giving up");
                        wifiAutoOnPending = false;
                    }
                }
                
                // Turn off WiFi 5 minutes after prayer starts (if autoWifiOffEnabled)
                if (wifiAutoOnPending && wifiIsOn && currentTotalMinutes >= nextPrayer) {
                    unsigned long prayerTimeMs = (currentTotalMinutes - nextPrayer) * 60000 + (adjustedTime->tm_sec * 1000);
                    if (prayerTimeMs >= WIFI_AUTO_DURATION_AFTER_AZAN && autoWifiOffEnabled) {
                        sysLog(LOG_INFO, "SYSTEM", "5 minutes after prayer - Auto-disabling WiFi");
                        toggleWiFi();
                        wifiAutoOnPending = false;
                        wifiConnectRetries = 0;
                    }
                }
            }
        }
    }
    
    // --- LEGACY: AUTO WI-FI OFF AFTER AZAN FINISHES (kept for compatibility) ---
    bool currentAudioRunning = audioMgr.isRunning();
    wasAudioPlaying = currentAudioRunning;
    
    unsigned long currentMs = millis();
    
    // --- PRINT CURRENT TIME EVERY SECOND ---
    if (currentMs - lastSecondPrint >= 1000) {
        lastSecondPrint = currentMs;
        struct tm timeinfo;
        if (getLocalTime(&timeinfo)) {
            bool audioRunning = audioMgr.isRunning();
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
    bool currentAudioRunningState = audioMgr.isRunning();
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
        else if (input == "WIFI_ON") {
            sysLog(LOG_INFO, "DEBUG", "Turning WiFi ON from terminal");
            if (!wifiIsOn) {
                toggleWiFi();
            } else {
                sysLog(LOG_INFO, "DEBUG", "WiFi is already ON");
            }
        }
        else if (input == "AUTO_WIFI_OFF_TOGGLE") {
            autoWifiOffEnabled = !autoWifiOffEnabled;
            sysLogf(LOG_INFO, "DEBUG", "Auto WiFi off toggled: %s", autoWifiOffEnabled ? "ON" : "OFF");
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
            sysLog(LOG_INFO, "DEBUG", "  WIFI_ON - Turn WiFi ON for debug");
            sysLog(LOG_INFO, "DEBUG", "  AUTO_WIFI_OFF_TOGGLE - Toggle auto WiFi off feature");
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
    const char* clk = timeMgr.rtcUsable() ? "RTC" : (timeMgr.activeSource() == TimeManager::Source::Ntp ? "NTP" : "NONE");
    sysLogf(LOG_INFO, "STATUS", "Wi-Fi: %s | SD Card: %s | Audio: %s | Clock: %s", 
           wifiIsOn ? "ON" : "OFF", 
           storageMgr.isReady() ? "OK" : "ERROR", 
           isAudioPlaying ? "PLAYING" : "IDLE",
           clk);
    if (timeMgr.rtcBatterySuspect()) {
        sysLog(LOG_WARN, "STATUS", "RTC battery suspect — replace CR2032 when possible");
    }
    sysLogf(LOG_INFO, "STATUS", "Pre-Fajr: %s | Current Azan: %s", 
           preFajrEnabled ? "ENABLED" : "DISABLED",
           azanFiles[currentAzanIndex]);
    sysLog(LOG_INFO, "STATUS", "===============================");
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
    bool audioRunning = audioMgr.isRunning();
    const char* audioStatus = isAudioPlaying ? (audioRunning ? "PLAYING" : "STOPPING") : "IDLE";
    
    // --- Build comprehensive health message ---
    sysLogf(LOG_INFO, "HEALTH", 
           "Heap: %lu/%lu (%u%%) | WiFi: %s (RSSI:%d)| SD: %s | File: %s",
           freeHeap, totalHeap, heapUsagePercent, 
           wifiStatus, rssi, audioStatus,       
           storageMgr.isReady() ? "OK" : "FAIL",
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

// --- WRAPPER TO PLAY AUDIO WITH DETAILED LOGGING ---
void playAudioFile(const char* filePath) {
    if (!audioMgr.playFromSd(filePath)) {
        isAudioPlaying = false;
    }
}