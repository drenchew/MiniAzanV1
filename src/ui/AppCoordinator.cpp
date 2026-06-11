#include "ui/AppCoordinator.h"
#include "AppTypes.h"
#include "AppLog.h"
#include "system/AzanSafeMode.h"
#include "system/BluetoothManager.h"
#include "system/BluetoothTransferJob.h"
#include "system/SchedPriority.h"
#include <time.h>

static void sdJobLogAdapter(int level, const char* tag, const char* msg) {
    appLog(level, tag, msg);
}

static const char* kPrayerNames[] = {"Fajr", "Duha", "Dhuhr", "Asr", "Maghrib", "Isha"};

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

uint8_t AppCoordinator::commandPriority(UiCmd cmd) {
    switch (cmd) {
        case UiCmd::StopAudio:
            return SchedPriority::P0_Emergency;
        case UiCmd::PlayFile:
        case UiCmd::PauseAudio:
        case UiCmd::ResumeAudio:
            return SchedPriority::P1_RealTime;
        case UiCmd::RequestClock:
        case UiCmd::RequestPrayerTimes:
            return SchedPriority::P2_SystemCore;
        case UiCmd::ListFolder:
        case UiCmd::ListAudioFiles:
        case UiCmd::RefreshFileList:
        case UiCmd::DeleteFile:
            return SchedPriority::P4_HeavyIo;
        default:
            return SchedPriority::P3_UserAction;
    }
}

void AppCoordinator::ingestCommands() {
    if (!_bridge) return;
    _deferredCount = 0;
    UiCommand cmd{};
    while (_deferredCount < kMaxDeferredCmds && _bridge->popCommand(cmd, 0)) {
        _deferred[_deferredCount++] = cmd;
    }
}

bool AppCoordinator::popDeferredAtPriority(uint8_t priorityBand, UiCommand& out) {
    for (uint8_t i = 0; i < _deferredCount; i++) {
        if (commandPriority(_deferred[i].cmd) == priorityBand) {
            out = _deferred[i];
            for (uint8_t j = i + 1; j < _deferredCount; j++) {
                _deferred[j - 1] = _deferred[j];
            }
            _deferredCount--;
            return true;
        }
    }
    return false;
}

void AppCoordinator::executeEmergencyStop(bool* isAudioPlaying) {
    if (_svc.audio) {
        _svc.audio->requestEmergencyStop();
    }
    if (isAudioPlaying) {
        *isAudioPlaying = false;
    } else if (_svc.isAudioPlaying) {
        *_svc.isAudioPlaying = false;
    }
    if (_svc.isAzanPlaying) {
        *_svc.isAzanPlaying = false;
    }
    UiEventPayload ev{};
    ev.type = UiEvent::AudioState;
    ev.audioPlaying = false;
    ev.audioPaused = false;
    ev.azanPlaying = false;
    emit(ev);
}

void AppCoordinator::dispatchCommand(const UiCommand& cmd) {
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
        case UiCmd::RequestPrayerTimes: handleRequestPrayer(cmd.prayer.yday); break;
        case UiCmd::ListAudioFiles: handleListAudioFiles(); break;
        case UiCmd::ListFolder: handleListFolder(cmd.list.path, cmd.list.page, cmd.list.requestId); break;
        case UiCmd::RefreshFileList: handleListFolder("/azan", 0, 0); break;
        case UiCmd::DeleteFile: handleDeleteFile(cmd.del.path); break;
        case UiCmd::SelectAzanFile: handleSelectAzanFile(cmd.azanPath.path); break;
        case UiCmd::PlayFile: handlePlayFile(cmd.play.path); break;
        case UiCmd::PauseAudio: handlePauseAudio(); break;
        case UiCmd::ResumeAudio: handleResumeAudio(); break;
        case UiCmd::ToggleBluetoothStreaming: handleToggleBluetoothStreaming(); break;
        case UiCmd::SetBluetoothStreamVolume: handleSetBluetoothStreamVolume(cmd.btStreamVol.volumePct); break;
        default: break;
    }
}

void AppCoordinator::pollStorageResults() {
    if (_svc.storageJobs) {
        _svc.storageJobs->poll();
    }
}

bool AppCoordinator::tickStorageWorker() {
    if (!_svc.storageJobs) {
        return false;
    }
    return _svc.storageJobs->tick();
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
    if (_svc.isAzanPlaying) *_svc.isAzanPlaying = false;
    UiEventPayload ev{};
    ev.type = UiEvent::AudioState;
    ev.audioPlaying = false;
    ev.audioPaused = false;
    ev.azanPlaying = false;
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
    bool next = !_svc.bluetooth->isEnabled() || _svc.bluetooth->isStreamingEnabled();
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

void AppCoordinator::handleToggleBluetoothStreaming() {
    if (!_svc.bluetooth) return;
    const bool next = !_svc.bluetooth->isStreamingEnabled();
    if (next && AzanSafeMode::isActive()) {
        UiEventPayload ev{};
        ev.type = UiEvent::CmdResult;
        ev.result = UiResult::Rejected;
        strncpy(ev.message, "Azan playing", sizeof(ev.message) - 1);
        emit(ev);
        handleRequestBluetoothStatus();
        return;
    }
    if (next && _svc.audio) {
        _svc.audio->requestStop();
        if (_svc.isAudioPlaying) *_svc.isAudioPlaying = false;
        if (_svc.isAzanPlaying) *_svc.isAzanPlaying = false;
        UiEventPayload ev{};
        ev.type = UiEvent::AudioState;
        ev.audioPlaying = false;
        ev.audioPaused = false;
        ev.azanPlaying = false;
        emit(ev);
    }
    const bool ok = _svc.bluetooth->requestStreamingMode(next);
    if (!ok) {
        UiEventPayload ev{};
        ev.type = UiEvent::CmdResult;
        ev.result = UiResult::Rejected;
        strncpy(ev.message, "Bluetooth busy", sizeof(ev.message) - 1);
        emit(ev);
    }
    handleRequestBluetoothStatus();
    handleRequestSystemStatus();
}

void AppCoordinator::handleSetBluetoothStreamVolume(uint8_t pct) {
    if (!_svc.bluetooth) return;
    _svc.bluetooth->requestSetStreamVolume(pct);
    handleRequestBluetoothStatus();
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
            strncpy(ev.clockSource, "FALLBK", sizeof(ev.clockSource) - 1);
        }
        ev.rtcOk = _svc.time->rtcUsable();
        ev.rtcBatteryFail = !ev.rtcOk && _svc.time->activeSource() != TimeManager::Source::NtpFallback;
    }
    emit(ev);
}

void AppCoordinator::handleRequestPrayer(int requestedYday) {
    if (!_svc.readDayRecord) return;
    PrayerNow now{};
    if (!_svc.time || !_svc.time->getPrayerNow(now) || !now.valid) return;

    int targetYday = requestedYday > 0 ? requestedYday : now.yday;
    if (targetYday < 1) targetYday = 1;
    if (targetYday > 366) targetYday = 366;
    const bool isToday = (targetYday == now.yday);

    DayRecord rec{};
    if (!_svc.readDayRecord(targetYday, rec)) return;

    if (isToday) {
        if (_svc.cachedPrayerTimes) *_svc.cachedPrayerTimes = rec;
        if (_svc.cachedPrayerDay) *_svc.cachedPrayerDay = targetYday;
        if (_svc.cachedPrayerTimesValid) *_svc.cachedPrayerTimesValid = true;
    }

    int nowMin = now.totalMinutes;
    UiEventPayload ev{};
    ev.type = UiEvent::PrayerTimesUpdate;
    for (int i = 0; i < 6; i++) ev.prayerMinutes[i] = rec.times[i];
    ev.prayerYday = targetYday;
    ev.prayerIsToday = isToday;

    struct tm selected{};
    selected.tm_year = now.year - 1900;
    selected.tm_mon = 0;
    selected.tm_mday = targetYday;
    selected.tm_isdst = -1;
    mktime(&selected);
    ev.prayerYear = selected.tm_year + 1900;
    ev.prayerMonth = selected.tm_mon + 1;
    ev.prayerMday = selected.tm_mday;

    ev.currentPrayerIndex = -1;
    if (isToday) {
        for (int i = 0; i < 6; i++) {
            if (i == 1) continue;
            if (nowMin >= rec.times[i]) ev.currentPrayerIndex = i;
        }
    }

    ev.nextPrayerIndex = -1;
    ev.nextPrayerMinutes = -1;
    ev.secondsToNext = -1;
    if (isToday) {
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
        ev.btEnabled = _svc.bluetooth->isEnabled() && !_svc.bluetooth->isStreamingEnabled();
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
    ev.audioPaused = false;
    ev.azanPlaying = _svc.isAzanPlaying && *_svc.isAzanPlaying;
    emit(ev);
}

void AppCoordinator::handlePauseAudio() {
    if (_svc.audio) _svc.audio->requestPause();
    if (_svc.isAudioPlaying) *_svc.isAudioPlaying = false;
    if (_svc.isAzanPlaying) *_svc.isAzanPlaying = false;
    UiEventPayload ev{};
    ev.type = UiEvent::AudioState;
    ev.audioPlaying = false;
    ev.audioPaused = true;
    ev.azanPlaying = false;
    emit(ev);
}

void AppCoordinator::handleResumeAudio() {
    if (_svc.audio) _svc.audio->requestResume();
    if (_svc.isAudioPlaying) *_svc.isAudioPlaying = true;
    if (_svc.isAzanPlaying) *_svc.isAzanPlaying = false;
    UiEventPayload ev{};
    ev.type = UiEvent::AudioState;
    ev.audioPlaying = true;
    ev.audioPaused = false;
    ev.azanPlaying = false;
    emit(ev);
}

void AppCoordinator::handleListFolder(const char* path, uint8_t page, uint32_t requestId) {
    if (!_svc.storageJobs) {
        UiEventPayload ev{};
        ev.type = UiEvent::StorageBusy;
        emit(ev);
        return;
    }
    if (storageBlocked() || !AzanSafeMode::allowStorageJobs()) {
        UiEventPayload ev{};
        ev.type = UiEvent::StorageBusy;
        emit(ev);
        return;
    }
    StorageJobQueue::Job job{};
    job.type = StorageJobQueue::JobType::ListDir;
    strncpy(job.path, path && path[0] ? path : "/", sizeof(job.path) - 1);
    job.page = page;
    job.pageSize = UI_FILE_PAGE_SIZE;
    job.requestId = requestId ? requestId : _nextInternalListRequestId++;
    if (_svc.storageJobs) {
        _svc.storageJobs->invalidateBefore(job.requestId);
    }
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

void AppCoordinator::handleListAudioFiles() {
    handleListFolder("/azan", 0, 0);
}

void AppCoordinator::handleRequestBluetoothStatus() {
    UiEventPayload ev{};
    ev.type = UiEvent::BluetoothStatus;
    if (_svc.bluetooth) {
        ev.btEnabled = _svc.bluetooth->isEnabled();
        ev.btConnected = _svc.bluetooth->isConnected();
        ev.btTransferActive = _svc.bluetooth->isTransferActive();
        ev.btStreamingEnabled = _svc.bluetooth->isStreamingEnabled();
        ev.btStreamingActive = _svc.bluetooth->isStreamingActive();
        ev.btStreamVolume = _svc.bluetooth->streamVolumePct();
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
