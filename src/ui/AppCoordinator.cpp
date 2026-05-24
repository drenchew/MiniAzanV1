#include "ui/AppCoordinator.h"
#include "AppTypes.h"
#include "AppLog.h"
#include "system/AzanSafeMode.h"
#include "system/BluetoothManager.h"
#include "system/BluetoothTransferJob.h"

static void sdJobLogAdapter(int level, const char* tag, const char* msg) {
    appLog(level, tag, msg);
}

static const char* kPrayerNames[] = {"Fajr", "Sunrise", "Dhuhr", "Asr", "Maghrib", "Isha"};

const char* AppCoordinator::prayerName(int idx) {
    if (idx < 0 || idx >= 6) return "?";
    return kPrayerNames[idx];
}

void AppCoordinator::storageEmitThunk(const UiEventPayload& ev, void* user) {
    static_cast<AppCoordinator*>(user)->emit(ev);
}

bool AppCoordinator::begin(UiBridge& bridge, const AppServices& svc) {
    _bridge = &bridge;
    _svc = svc;
    if (_svc.storageJobs && _svc.storage) {
        _svc.storageJobs->begin(_svc.storage, storageEmitThunk, this, sdJobLogAdapter);
    }
    return _svc.audio && _svc.storage && _svc.time && _bridge;
}

bool AppCoordinator::storageBlocked() const {
    return _svc.storage && _svc.storage->isPlaybackLocked();
}

void AppCoordinator::emit(const UiEventPayload& ev) {
    if (_bridge) _bridge->postEvent(ev, 0);
}

void AppCoordinator::handleStopAudio() {
    if (_svc.audio) _svc.audio->requestStop();
    if (_svc.isAudioPlaying) *_svc.isAudioPlaying = false;
    UiEventPayload ev{};
    ev.type = UiEvent::AudioState;
    ev.audioPlaying = false;
    emit(ev);
}

void AppCoordinator::handleSetVolumePct(uint8_t pct) {
    if (!_svc.currentVolume) return;
    if (pct > 100) pct = 100;
    uint8_t v = (uint8_t)((pct * _svc.maxVolume + 50) / 100);
    if (v < _svc.minVolume) v = _svc.minVolume;
  if (v > _svc.maxVolume) v = _svc.maxVolume;
    *_svc.currentVolume = v;
    if (_svc.audio) _svc.audio->requestSetVolume(v);
    if (_svc.saveVolumeToNvs) _svc.saveVolumeToNvs(v);
    UiEventPayload ev{};
    ev.type = UiEvent::VolumeState;
    ev.volume = v;
    ev.volumePct = pct;
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

void AppCoordinator::handleToggleTransferMode() {
    if (!_svc.bluetooth) return;
    bool next = !_svc.bluetooth->isEnabled();
    if (next && AzanSafeMode::isActive()) {
        UiEventPayload ev{};
        ev.type = UiEvent::CmdResult;
        ev.result = UiResult::Rejected;
        strncpy(ev.message, "Azan playing", sizeof(ev.message) - 1);
        emit(ev);
        handleRequestBluetoothStatus();
        return;
    }
    _svc.bluetooth->requestTransferMode(next);
    handleRequestBluetoothStatus();
    handleRequestSystemStatus();
}

void AppCoordinator::handleCancelBluetoothTransfer() {
    if (_svc.bluetooth) {
        _svc.bluetooth->requestCancelTransfer();
    }
    handleRequestBluetoothStatus();
}

void AppCoordinator::handleRequestClock() {
    PrayerNow now{};
    UiEventPayload ev{};
    ev.type = UiEvent::ClockUpdate;
    if (_svc.time && _svc.time->getPrayerNow(now) && now.valid) {
        ev.year = now.year;
        ev.hour = now.hour;
        ev.minute = now.minute;
        ev.second = now.second;
        ev.yday = now.yday;
        ev.month = now.month;
        ev.mday = now.day;
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

    DayRecord rec{};
    if (!_svc.readDayRecord(now.yday, rec)) return;

    if (_svc.cachedPrayerTimes) *_svc.cachedPrayerTimes = rec;
    if (_svc.cachedPrayerDay) *_svc.cachedPrayerDay = now.yday;
    if (_svc.cachedPrayerTimesValid) *_svc.cachedPrayerTimesValid = true;

    int nowMin = now.totalMinutes;
    UiEventPayload ev{};
    ev.type = UiEvent::PrayerTimesUpdate;
    for (int i = 0; i < 6; i++) ev.prayerMinutes[i] = rec.times[i];

    ev.currentPrayerIndex = -1;
    for (int i = 0; i < 6; i++) {
        if (i == 1) continue;
        if (nowMin >= rec.times[i]) ev.currentPrayerIndex = i;
    }

    ev.nextPrayerIndex = -1;
    ev.nextPrayerMinutes = -1;
    ev.secondsToNext = -1;
    for (int i = 0; i < 6; i++) {
        if (i == 1) continue;
        if (rec.times[i] > nowMin) {
            ev.nextPrayerIndex = i;
            ev.nextPrayerMinutes = rec.times[i] - nowMin;
            ev.secondsToNext = ev.nextPrayerMinutes * 60 - now.second;
            if (ev.secondsToNext < 0) ev.secondsToNext = 0;
            break;
        }
    }
    if (ev.currentPrayerIndex >= 0) {
        strncpy(ev.currentPrayerName, prayerName(ev.currentPrayerIndex), sizeof(ev.currentPrayerName) - 1);
    }
    if (ev.nextPrayerIndex >= 0) {
        strncpy(ev.nextPrayerName, prayerName(ev.nextPrayerIndex), sizeof(ev.nextPrayerName) - 1);
    }
    emit(ev);
}

void AppCoordinator::handleRequestSystemStatus() {
    UiEventPayload ev{};
    ev.type = UiEvent::SystemStatus;
    if (_svc.storage) ev.sdReady = _svc.storage->isReady();
    if (_svc.time) {
        ev.rtcOk = _svc.time->rtcUsable();
        ev.rtcBatteryFail = !ev.rtcOk && _svc.time->activeSource() != TimeManager::Source::NtpFallback;
        if (_svc.time->rtcUsable()) strncpy(ev.timeSource, "RTC OK", sizeof(ev.timeSource) - 1);
        else if (_svc.time->activeSource() == TimeManager::Source::NtpFallback) {
            strncpy(ev.timeSource, "NTP", sizeof(ev.timeSource) - 1);
        } else {
            strncpy(ev.timeSource, "FALLBACK", sizeof(ev.timeSource) - 1);
        }
    }
    if (_svc.currentVolume) {
        ev.volume = *_svc.currentVolume;
        ev.volumePct = (uint8_t)((ev.volume * 100) / (_svc.maxVolume ? _svc.maxVolume : 21));
    }
    if (_svc.preFajrEnabled) ev.preFajr = *_svc.preFajrEnabled;
    if (_svc.uiSelectedAzanPath && _svc.uiSelectedAzanPath[0]) {
        strncpy(ev.defaultAzanPath, _svc.uiSelectedAzanPath, sizeof(ev.defaultAzanPath) - 1);
    } else if (_svc.azanFiles && _svc.currentAzanIndex && *_svc.currentAzanIndex < _svc.numAzanFiles) {
        strncpy(ev.defaultAzanPath, _svc.azanFiles[*_svc.currentAzanIndex], sizeof(ev.defaultAzanPath) - 1);
    }
    if (_svc.bluetooth) {
        ev.btEnabled = _svc.bluetooth->isEnabled();
        ev.btConnected = _svc.bluetooth->isConnected();
        ev.btTransferActive = _svc.bluetooth->isTransferActive();
        ev.btProgressPct = _svc.bluetooth->activeJob().progressPct;
    }
    emit(ev);
    handleRequestBluetoothStatus();
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
    strncpy(ev.defaultAzanPath, _svc.uiSelectedAzanPath, sizeof(ev.defaultAzanPath) - 1);
    emit(ev);
}

void AppCoordinator::handlePlayFile(const char* path) {
    if (!path || !path[0]) return;
    if (storageBlocked() || AzanSafeMode::isActive()) {
        UiEventPayload ev{};
        ev.type = UiEvent::StorageBusy;
        emit(ev);
        return;
    }
    if (_svc.audio) {
        _svc.audio->requestStop();
        if (_svc.audio->requestPlay(path)) {
            if (_svc.isAudioPlaying) {
                *_svc.isAudioPlaying = true;
            }
        }
    }
    UiEventPayload ev{};
    ev.type = UiEvent::AudioState;
    ev.audioPlaying = _svc.isAudioPlaying && *_svc.isAudioPlaying;
    emit(ev);
}

void AppCoordinator::handleListFolder(const char* path, uint8_t page) {
    if (!_svc.storageJobs) {
        handleListFiles(false);
        return;
    }
    if (storageBlocked()) {
        UiEventPayload ev{};
        ev.type = UiEvent::StorageBusy;
        emit(ev);
        return;
    }
    StorageJobQueue::Job job{};
    job.type = StorageJobQueue::JobType::ListDir;
    strncpy(job.path, path && path[0] ? path : "/", sizeof(job.path) - 1);
    job.page = page;
    job.pageSize = 16;
    if (!_svc.storageJobs->submit(job)) {
        UiEventPayload ev{};
        ev.type = UiEvent::StorageBusy;
        emit(ev);
    }
}

void AppCoordinator::handleDeleteFile(const char* path) {
    if (!path || !path[0]) return;
    if (storageBlocked()) {
        UiEventPayload ev{};
        ev.type = UiEvent::FileOpResult;
        ev.result = UiResult::Busy;
        emit(ev);
        return;
    }
    if (_svc.storageJobs) {
        StorageJobQueue::Job job{};
        job.type = StorageJobQueue::JobType::DeleteFile;
        strncpy(job.path, path, sizeof(job.path) - 1);
        _svc.storageJobs->submit(job);
        return;
    }
    bool ok = _svc.storage && _svc.storage->removeFile(path);
    UiEventPayload ev{};
    ev.type = UiEvent::FileOpResult;
    ev.result = ok ? UiResult::Ok : UiResult::Failed;
    strncpy(ev.message, path, sizeof(ev.message) - 1);
    emit(ev);
}

void AppCoordinator::handleListFiles(bool audioOnly) {
    if (storageBlocked()) {
        UiEventPayload ev{};
        ev.type = UiEvent::StorageBusy;
        emit(ev);
        return;
    }
    if (!_svc.storage || !_svc.storage->isReady()) {
        UiEventPayload ev{};
        ev.type = UiEvent::FileListReady;
        ev.fileCount = 0;
        emit(ev);
        return;
    }
    String json;
    if (!_svc.storage->listRootFilesJson(json)) {
        UiEventPayload ev{};
        ev.type = UiEvent::StorageBusy;
        emit(ev);
        return;
    }
    UiEventPayload ev{};
    ev.type = UiEvent::FileListReady;
    int start = json.indexOf('[');
    int pos = start >= 0 ? start + 1 : 0;
    while (ev.fileCount < 16 && pos < (int)json.length()) {
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

void AppCoordinator::handleListAudioFiles() {
    if (_svc.storageJobs) {
        handleListFolder("/azan", 0);
    } else {
        handleListFiles(true);
    }
}

void AppCoordinator::dispatch(const UiCommand& cmd) {
    switch (cmd.cmd) {
        case UiCmd::StopAudio: handleStopAudio(); break;
        case UiCmd::SetVolume: handleSetVolumePct(cmd.vol.volumePct); break;
        case UiCmd::SetPreFajr: handlePreFajr(cmd.pf.enabled); break;
        case UiCmd::SetAzanIndex: handleAzanIndex(cmd.az.index); break;
        case UiCmd::ToggleTransferMode: handleToggleTransferMode(); break;
        case UiCmd::RequestBluetoothStatus: handleRequestBluetoothStatus(); break;
        case UiCmd::CancelBluetoothTransfer: handleCancelBluetoothTransfer(); break;
        case UiCmd::RequestSystemStatus: handleRequestSystemStatus(); break;
        case UiCmd::RequestClock: handleRequestClock(); break;
        case UiCmd::RequestPrayerTimes: handleRequestPrayer(); break;
        case UiCmd::ListAudioFiles: handleListAudioFiles(); break;
        case UiCmd::ListFolder: handleListFolder(cmd.list.path, cmd.list.page); break;
        case UiCmd::RefreshFileList: handleListFolder("/azan", 0); break;
        case UiCmd::DeleteFile: handleDeleteFile(cmd.del.path); break;
        case UiCmd::SelectAzanFile: handleSelectAzanFile(cmd.azanPath.path); break;
        case UiCmd::PlayFile: handlePlayFile(cmd.play.path); break;
        case UiCmd::PauseAudio:
        case UiCmd::ResumeAudio:
            break;
        default: break;
    }
}

void AppCoordinator::handleRequestBluetoothStatus() {
    UiEventPayload ev{};
    ev.type = UiEvent::BluetoothStatus;
    if (_svc.bluetooth) {
        ev.btEnabled = _svc.bluetooth->isEnabled();
        ev.btConnected = _svc.bluetooth->isConnected();
        ev.btTransferActive = _svc.bluetooth->isTransferActive();
        const BluetoothTransferJob& job = _svc.bluetooth->activeJob();
        ev.btProgressPct = job.progressPct;
        switch (job.phase) {
            case BtTransferPhase::Idle:
                strncpy(ev.btStatusMsg, "Offline", sizeof(ev.btStatusMsg) - 1);
                break;
            case BtTransferPhase::AwaitingConnection:
                strncpy(ev.btStatusMsg, "Awaiting connection", sizeof(ev.btStatusMsg) - 1);
                break;
            case BtTransferPhase::Receiving:
                strncpy(ev.btStatusMsg, "Receiving file", sizeof(ev.btStatusMsg) - 1);
                break;
            case BtTransferPhase::QueuedForSd:
                strncpy(ev.btStatusMsg, "Queued for SD", sizeof(ev.btStatusMsg) - 1);
                break;
            case BtTransferPhase::Writing:
                strncpy(ev.btStatusMsg, "Writing to SD", sizeof(ev.btStatusMsg) - 1);
                break;
            case BtTransferPhase::Complete:
                strncpy(ev.btStatusMsg, "Transfer complete", sizeof(ev.btStatusMsg) - 1);
                break;
            case BtTransferPhase::PausedForAzan:
                strncpy(ev.btStatusMsg, "Paused (azan)", sizeof(ev.btStatusMsg) - 1);
                break;
            case BtTransferPhase::Error:
                strncpy(ev.btStatusMsg, job.errorMsg[0] ? job.errorMsg : "Error",
                        sizeof(ev.btStatusMsg) - 1);
                break;
            default:
                break;
        }
    }
    emit(ev);
}

void AppCoordinator::poll() {
    if (!_bridge) return;

    if (_svc.storageJobs) {
        _svc.storageJobs->poll();
    }

    UiCommand cmd{};
    while (_bridge->popUrgent(cmd, 0)) dispatch(cmd);
    unsigned processed = 0;
    while (processed < 4 && _bridge->popCommand(cmd, 0)) {
        dispatch(cmd);
        processed++;
    }
}
