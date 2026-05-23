#include "ui/AppCoordinator.h"
#include "AppTypes.h"
#include <WiFi.h>

bool AppCoordinator::begin(UiBridge& bridge, const AppServices& svc) {
    _bridge = &bridge;
    _svc = svc;
    return _svc.audio && _svc.storage && _svc.time && _bridge;
}

bool AppCoordinator::storageBlocked() const {
    return _svc.storage && _svc.storage->isPlaybackLocked();
}

void AppCoordinator::emit(const UiEventPayload& ev) {
    if (_bridge) _bridge->postEvent(ev, 0);
}

void AppCoordinator::handleStopAudio() {
    if (_svc.audio) _svc.audio->stop();
    if (_svc.isAudioPlaying) *_svc.isAudioPlaying = false;

    UiEventPayload ev{};
    ev.type = UiEvent::AudioState;
    ev.audioPlaying = false;
    ev.result = UiResult::Ok;
    emit(ev);
}

void AppCoordinator::handleSetVolume(uint8_t v) {
    if (!_svc.currentVolume) return;
    if (v < _svc.minVolume) v = _svc.minVolume;
    if (v > _svc.maxVolume) v = _svc.maxVolume;
    *_svc.currentVolume = v;
    if (_svc.audio) _svc.audio->setVolume(v);
    if (_svc.saveVolumeToNvs) _svc.saveVolumeToNvs(v);

    UiEventPayload ev{};
    ev.type = UiEvent::VolumeState;
    ev.volume = v;
    emit(ev);
}

void AppCoordinator::handlePreFajr(bool on) {
    if (_svc.preFajrEnabled) *_svc.preFajrEnabled = on;
    if (_svc.savePreFajrToNvs) _svc.savePreFajrToNvs(on);

    UiEventPayload ev{};
    ev.type = UiEvent::PreFajrState;
    ev.preFajr = on;
    emit(ev);
}

void AppCoordinator::handleAzanIndex(uint8_t idx) {
    if (!_svc.currentAzanIndex || !_svc.numAzanFiles) return;
    if (idx >= (uint8_t)_svc.numAzanFiles) return;
    *_svc.currentAzanIndex = (int)idx;
    if (_svc.saveAzanIndexToNvs) _svc.saveAzanIndexToNvs(idx);

    UiEventPayload ev{};
    ev.type = UiEvent::AzanIndexState;
    ev.azanIndex = idx;
    emit(ev);
}

void AppCoordinator::handleToggleWifi() {
    if (_svc.toggleWifi) _svc.toggleWifi();
    handleRequestWifiStatus();
}

void AppCoordinator::handleRequestClock() {
    PrayerNow now{};
    UiEventPayload ev{};
    ev.type = UiEvent::ClockUpdate;
    if (_svc.time && _svc.time->getPrayerNow(now) && now.valid) {
        ev.hour = now.hour;
        ev.minute = now.minute;
        ev.second = now.second;
        if (_svc.time->rtcUsable()) strncpy(ev.clockSource, "RTC", sizeof(ev.clockSource) - 1);
        else if (_svc.time->activeSource() == TimeManager::Source::NtpFallback) {
            strncpy(ev.clockSource, "NTP", sizeof(ev.clockSource) - 1);
        } else {
            strncpy(ev.clockSource, "NONE", sizeof(ev.clockSource) - 1);
        }
    }
    emit(ev);
}

void AppCoordinator::handleRequestPrayer() {
    if (!_svc.readDayRecord) return;

    PrayerNow now{};
    if (!_svc.time || !_svc.time->getPrayerNow(now) || !now.valid) return;

    int day = now.yday;
    int nowMin = now.totalMinutes;

    DayRecord rec{};
    if (!_svc.readDayRecord(day, rec)) return;

    if (_svc.cachedPrayerTimes) *_svc.cachedPrayerTimes = rec;
    if (_svc.cachedPrayerDay) *_svc.cachedPrayerDay = day;
    if (_svc.cachedPrayerTimesValid) *_svc.cachedPrayerTimesValid = true;

    UiEventPayload ev{};
    ev.type = UiEvent::PrayerTimesUpdate;
    for (int i = 0; i < 6; i++) ev.prayerMinutes[i] = rec.times[i];
    ev.nextPrayerMinutes = -1;
    for (int i = 0; i < 6; i++) {
        if (i == 1) continue;
        if (rec.times[i] > nowMin) {
            ev.nextPrayerMinutes = rec.times[i] - nowMin;
            break;
        }
    }
    emit(ev);
}

void AppCoordinator::handleSelectAzanFile(const char* path) {
    if (!path || !path[0] || !_svc.uiSelectedAzanPath || _svc.uiSelectedAzanPathSize < 2) return;
    if (path[0] == '/') {
        strncpy(_svc.uiSelectedAzanPath, path, _svc.uiSelectedAzanPathSize - 1);
    } else {
        snprintf(_svc.uiSelectedAzanPath, _svc.uiSelectedAzanPathSize, "/%s", path);
    }
    _svc.uiSelectedAzanPath[_svc.uiSelectedAzanPathSize - 1] = '\0';
    if (_svc.saveAzanPathToNvs) _svc.saveAzanPathToNvs(_svc.uiSelectedAzanPath);
    UiEventPayload ev{};
    ev.type = UiEvent::AzanIndexState;
    emit(ev);
}

void AppCoordinator::handleListFiles(bool audioOnly) {
    if (storageBlocked()) {
        UiEventPayload ev{};
        ev.type = UiEvent::StorageBusy;
        ev.result = UiResult::Busy;
        emit(ev);
        return;
    }
    if (!_svc.storage || !_svc.storage->isReady()) {
        UiEventPayload ev{};
        ev.type = UiEvent::FileListReady;
        ev.fileCount = 0;
        ev.result = UiResult::Failed;
        emit(ev);
        return;
    }

    String json;
    if (!_svc.storage->listRootFilesJson(json)) {
        UiEventPayload ev{};
        ev.type = UiEvent::FileListReady;
        ev.result = UiResult::Busy;
        emit(ev);
        return;
    }

    UiEventPayload ev{};
    ev.type = UiEvent::FileListReady;
    ev.result = UiResult::Ok;

    int start = json.indexOf('[');
    int pos = start >= 0 ? start + 1 : 0;
    while (ev.fileCount < 24 && pos < (int)json.length()) {
        int nameKey = json.indexOf("\"name\":\"", pos);
        if (nameKey < 0) break;
        nameKey += 8;
        int nameEnd = json.indexOf('"', nameKey);
        if (nameEnd < 0) break;
        String name = json.substring(nameKey, nameEnd);
        int szKey = json.indexOf("\"size\":", nameEnd);
        uint32_t sz = 0;
        if (szKey >= 0) sz = (uint32_t)json.substring(szKey + 7).toInt();

        bool isAudio = name.endsWith(".mp3") || name.endsWith(".wav") ||
                       name.endsWith(".MP3") || name.endsWith(".WAV");
        if (!audioOnly || isAudio) {
            UiFileEntry& e = ev.files[ev.fileCount++];
            name.toCharArray(e.name, sizeof(e.name));
            e.size = sz;
        }
        pos = nameEnd + 1;
    }
    emit(ev);
}

void AppCoordinator::handleDeleteFile(const char* name) {
    if (!name || !name[0]) return;
    if (storageBlocked()) {
        UiEventPayload ev{};
        ev.type = UiEvent::FileOpResult;
        ev.result = UiResult::Busy;
        emit(ev);
        return;
    }
    bool ok = _svc.storage && _svc.storage->removeFile(name);
    UiEventPayload ev{};
    ev.type = UiEvent::FileOpResult;
    ev.result = ok ? UiResult::Ok : UiResult::Failed;
    strncpy(ev.message, name, sizeof(ev.message) - 1);
    emit(ev);
}

void AppCoordinator::dispatch(const UiCommand& cmd) {
    switch (cmd.cmd) {
        case UiCmd::StopAudio:
            handleStopAudio();
            break;
        case UiCmd::SetVolume:
            handleSetVolume(cmd.vol.volume);
            break;
        case UiCmd::SetPreFajr:
            handlePreFajr(cmd.pf.enabled);
            break;
        case UiCmd::SetAzanIndex:
            handleAzanIndex(cmd.az.index);
            break;
        case UiCmd::ToggleWifi:
            handleToggleWifi();
            break;
        case UiCmd::RequestWifiStatus:
            handleRequestWifiStatus();
            break;
        case UiCmd::RequestClock:
            handleRequestClock();
            break;
        case UiCmd::RequestPrayerTimes:
            handleRequestPrayer();
            break;
        case UiCmd::ListAudioFiles:
            handleListFiles(true);
            break;
        case UiCmd::RefreshFileList:
            handleListFiles(false);
            break;
        case UiCmd::DeleteFile:
            handleDeleteFile(cmd.del.name);
            break;
        case UiCmd::SelectAzanFile:
            handleSelectAzanFile(cmd.azanPath.path);
            break;
        default:
            break;
    }
}

void AppCoordinator::handleRequestWifiStatus() {
    UiEventPayload ev{};
    ev.type = UiEvent::WifiStatus;
    if (_svc.wifiIsOn) ev.wifiOn = *_svc.wifiIsOn;
    if (ev.wifiOn && WiFi.status() == WL_CONNECTED) {
        ev.wifiRssi = WiFi.RSSI();
        strncpy(ev.ip, WiFi.localIP().toString().c_str(), sizeof(ev.ip) - 1);
    }
    emit(ev);
}

void AppCoordinator::poll() {
    if (!_bridge) return;

    UiCommand cmd{};
    while (_bridge->popUrgent(cmd, 0)) {
        dispatch(cmd);
    }

    unsigned processed = 0;
    constexpr unsigned kMaxPerPoll = 4;
    while (processed < kMaxPerPoll && _bridge->popCommand(cmd, 0)) {
        dispatch(cmd);
        processed++;
    }
}
