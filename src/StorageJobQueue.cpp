#include "StorageJobQueue.h"
#include "system/AzanSafeMode.h"
#include <stdarg.h>
#include <string.h>

bool StorageJobQueue::begin(StorageManager* storage, EmitFn emit, void* user, LogFn log) {
    _storage = storage;
    _emit = emit;
    _emitUser = user;
    _log = log;

    if (!_jobQ) {
        _jobQ = xQueueCreate(4, sizeof(Job));
    }
    if (!_resultQ) {
        _resultQ = xQueueCreate(8, sizeof(JobResult));
    }
    if (!_jobQ || !_resultQ) {
        logMsg(0, "queue create failed");
        return false;
    }

    logf(2, "SDJob ready (cooperative state machine)");
    return _storage != nullptr && _emit != nullptr;
}

bool StorageJobQueue::submit(const Job& job) {
    if (!_jobQ) return false;
    if (!AzanSafeMode::allowStorageJobs()) {
        logf(1, "submit REJECTED azan_lock type=%u path=%s",
             (unsigned)job.type, job.path);
        return false;
    }
    Job copy = job;
    if (copy.requestId == 0) {
        copy.requestId = _nextId++;
    }
    if (copy.requestId < _minValidRequestId) {
        logf(1, "submit DROPPED stale id=%lu min=%lu path=%s",
             (unsigned long)copy.requestId,
             (unsigned long)_minValidRequestId,
             copy.path);
        return false;
    }

    const bool ok = xQueueSend(_jobQ, &copy, 0) == pdTRUE;
    if (ok) {
        logf(2, "submit id=%lu type=%u path=%s",
             (unsigned long)copy.requestId, (unsigned)copy.type, copy.path);
    } else {
        logf(1, "submit REJECTED queue full type=%u", (unsigned)job.type);
    }
    return ok;
}

void StorageJobQueue::invalidateBefore(uint32_t requestId) {
    if (requestId == 0 || requestId <= _minValidRequestId) {
        return;
    }
    _minValidRequestId = requestId;

    Job keep[4]{};
    uint8_t keepCount = 0;
    Job pending{};
    while (_jobQ && xQueueReceive(_jobQ, &pending, 0) == pdTRUE) {
        if (pending.requestId >= _minValidRequestId && keepCount < 4) {
            keep[keepCount++] = pending;
        } else {
            logf(2, "drop queued stale id=%lu min=%lu path=%s",
                 (unsigned long)pending.requestId,
                 (unsigned long)_minValidRequestId,
                 pending.path);
        }
    }
    for (uint8_t i = 0; i < keepCount; i++) {
        xQueueSend(_jobQ, &keep[i], 0);
    }
}

void StorageJobQueue::logMsg(int level, const char* msg) const {
    if (_log) {
        _log(level, "SDJOB", msg);
    }
}

void StorageJobQueue::logf(int level, const char* fmt, ...) const {
    if (!_log) return;
    char buf[160];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    _log(level, "SDJOB", buf);
}

void StorageJobQueue::pushResult(const JobResult& res) {
    if (!_resultQ) return;
    if (res.requestId < _minValidRequestId) {
        logf(2, "drop stale result id=%lu min=%lu",
             (unsigned long)res.requestId,
             (unsigned long)_minValidRequestId);
        return;
    }
    if (xQueueSend(_resultQ, &res, 0) != pdTRUE) {
        logf(1, "result queue full id=%lu", (unsigned long)res.requestId);
    }
}

void StorageJobQueue::startJob(const Job& job) {
    _active = job;
    _listCursor = 0;
    _listTotal = 0;
    _batchCount = 0;

    if (job.type == JobType::ListDir) {
        _phase = StreamPhase::ListStart;
        logf(2, "start list id=%lu path=%s", (unsigned long)job.requestId, job.path);
    } else {
        _phase = StreamPhase::DeleteRun;
        logf(2, "start delete id=%lu path=%s", (unsigned long)job.requestId, job.path);
    }
}

bool StorageJobQueue::tickList() {
    if (!_storage || !_storage->isReady()) {
        finishList(false);
        return false;
    }

    if (_phase == StreamPhase::ListStart) {
        JobResult res{};
        res.type = JobType::ListDir;
        res.phase = StreamPhase::ListStart;
        res.requestId = _active.requestId;
        res.ok = true;
        strncpy(res.folder, _active.path, sizeof(res.folder) - 1);
        pushResult(res);
        _phase = StreamPhase::ListEntry;
        return true;
    }

    if (_phase == StreamPhase::ListEntry) {
        StorageManager::DirEntry pageEntries[UI_FILE_PAGE_SIZE]{};
        uint8_t pageSize = _active.pageSize;
        if (pageSize == 0 || pageSize > UI_FILE_PAGE_SIZE) {
            pageSize = UI_FILE_PAGE_SIZE;
        }
        const int skip = (int)_active.page * (int)pageSize;
        const int rc = _storage->listDirectoryPage(
            _active.path, pageEntries, pageSize, skip, &_listTotal);

        if (rc < 0 && !AzanSafeMode::allowStorageJobs()) {
            _phase = StreamPhase::Paused;
            logf(2, "list paused id=%lu (azan)", (unsigned long)_active.requestId);
            return true;
        }
        if (rc < 0) {
            finishList(false);
            return false;
        }

        _batchCount = 0;
        for (int i = 0; i < rc && i < UI_FILE_PAGE_SIZE; i++) {
            UiFileEntry& dst = _batch[_batchCount++];
            strncpy(dst.name, pageEntries[i].name, sizeof(dst.name) - 1);
            dst.name[sizeof(dst.name) - 1] = '\0';
            dst.size = pageEntries[i].size;
            dst.isFolder = pageEntries[i].isFolder;
        }
        finishList(true);
        return false;
    }

    if (_phase == StreamPhase::Paused) {
        if (AzanSafeMode::allowStorageJobs()) {
            _phase = StreamPhase::ListEntry;
            return true;
        }
        return true;
    }

    return false;
}

void StorageJobQueue::finishList(bool ok) {
    JobResult res{};
    res.type = JobType::ListDir;
    res.phase = StreamPhase::ListEnd;
    res.requestId = _active.requestId;
    res.ok = ok;
    strncpy(res.folder, _active.path, sizeof(res.folder) - 1);
    res.fileCount = _batchCount;
    res.listTotal = (uint8_t)(_listTotal > 255 ? 255 : _listTotal);
    res.listPage = _active.page;
    for (uint8_t i = 0; i < _batchCount && i < UI_FILE_PAGE_SIZE; i++) {
        res.files[i] = _batch[i];
    }
    
    logf(2, "list finish id=%lu ok=%d batchCount=%u total=%d", 
         (unsigned long)_active.requestId, ok ? 1 : 0, (unsigned)_batchCount, _listTotal);
    
    pushResult(res);

    logf(2, "list done id=%lu ok=%d files=%u total=%d",
         (unsigned long)_active.requestId, ok ? 1 : 0, (unsigned)_batchCount, _listTotal);

    _phase = StreamPhase::Idle;
    _batchCount = 0;
}

bool StorageJobQueue::tickDelete() {
    if (!_storage || !_active.path[0]) {
        JobResult res{};
        res.type = JobType::DeleteFile;
        res.requestId = _active.requestId;
        res.ok = false;
        strncpy(res.message, _active.path, sizeof(res.message) - 1);
        pushResult(res);
        _phase = StreamPhase::Idle;
        return false;
    }

    if (_storage->isPlaybackLocked()) {
        _phase = StreamPhase::Paused;
        return true;
    }

    if (_phase == StreamPhase::Paused) {
        if (!_storage->isPlaybackLocked()) {
            _phase = StreamPhase::DeleteRun;
        }
        return _phase == StreamPhase::Paused;
    }

    const bool ok = _storage->removeFile(_active.path);
    JobResult res{};
    res.type = JobType::DeleteFile;
    res.requestId = _active.requestId;
    res.ok = ok;
    strncpy(res.message, _active.path, sizeof(res.message) - 1);
    pushResult(res);

    logf(2, "delete done id=%lu ok=%d", (unsigned long)_active.requestId, ok ? 1 : 0);
    _phase = StreamPhase::Idle;
    return false;
}

bool StorageJobQueue::tick() {
    if (_phase != StreamPhase::Idle && _active.requestId < _minValidRequestId) {
        logf(2, "cancel active stale id=%lu min=%lu path=%s",
             (unsigned long)_active.requestId,
             (unsigned long)_minValidRequestId,
             _active.path);
        _phase = StreamPhase::Idle;
        _batchCount = 0;
    }

    if (_phase == StreamPhase::Idle) {
        Job job{};
        if (_jobQ && xQueueReceive(_jobQ, &job, 0) == pdTRUE) {
            if (job.requestId < _minValidRequestId) {
                logf(2, "skip stale job id=%lu min=%lu path=%s",
                     (unsigned long)job.requestId,
                     (unsigned long)_minValidRequestId,
                     job.path);
                return true;
            }
            startJob(job);
        } else {
            return false;
        }
    }

    if (_active.type == JobType::ListDir) {
        return tickList();
    }
    return tickDelete();
}

void StorageJobQueue::poll() {
    if (!_resultQ || !_emit) return;

    JobResult res{};
    while (xQueueReceive(_resultQ, &res, 0) == pdTRUE) {
        if (res.requestId < _minValidRequestId) {
            logf(2, "poll drop stale result id=%lu min=%lu",
                 (unsigned long)res.requestId,
                 (unsigned long)_minValidRequestId);
            continue;
        }
        UiEventPayload ev{};

        if (res.type == JobType::ListDir) {
            if (res.phase == StreamPhase::ListStart) {
                ev.type = UiEvent::FileListStreamStart;
                ev.listRequestId = res.requestId;
                strncpy(ev.listFolder, res.folder, sizeof(ev.listFolder) - 1);
            } else if (res.phase == StreamPhase::ListEntry) {
                ev.type = UiEvent::FileListStreamEntry;
                ev.listRequestId = res.requestId;
                ev.fileCount = 1;
                ev.files[0] = res.entry;
                strncpy(ev.listFolder, res.folder, sizeof(ev.listFolder) - 1);
            } else if (res.phase == StreamPhase::ListEnd) {
                // #region agent log
                logf(2,
                     "{\"sessionId\":\"36936e\",\"runId\":\"initial\",\"hypothesisId\":\"H2,H3\","
                     "\"location\":\"StorageJobQueue.cpp:248\",\"message\":\"list end result before ui emit\","
                     "\"data\":{\"requestId\":%lu,\"ok\":%d,\"fileCount\":%u,\"total\":%u,"
                     "\"folder\":\"%s\",\"first\":\"%s\",\"firstFolder\":%d}}",
                     (unsigned long)res.requestId,
                     res.ok ? 1 : 0,
                     (unsigned)res.fileCount,
                     (unsigned)res.listTotal,
                     res.folder,
                     res.fileCount ? res.files[0].name : "",
                     res.fileCount ? (res.files[0].isFolder ? 1 : 0) : -1);
                // #endregion
                UiEventPayload endEv{};
                endEv.type = UiEvent::FileListStreamEnd;
                endEv.result = res.ok ? UiResult::Ok : UiResult::Failed;
                endEv.listRequestId = res.requestId;
                endEv.listTotal = res.listTotal;
                strncpy(endEv.listFolder, res.folder, sizeof(endEv.listFolder) - 1);
                _emit(endEv, _emitUser);

                ev.type = UiEvent::FileListReady;
                ev.result = res.ok ? UiResult::Ok : UiResult::Failed;
                ev.listRequestId = res.requestId;
                ev.fileCount = res.fileCount;
                ev.listTotal = res.listTotal;
                ev.listPage = res.listPage;
                strncpy(ev.listFolder, res.folder, sizeof(ev.listFolder) - 1);
                for (uint8_t i = 0; i < res.fileCount && i < UI_FILE_PAGE_SIZE; i++) {
                    ev.files[i] = res.files[i];
                }
                
                logf(2, "Emitting FileListReady: fileCount=%d total=%d folder=%s", 
                     res.fileCount, res.listTotal, res.folder);
            }
        } else if (res.type == JobType::DeleteFile) {
            ev.type = UiEvent::FileOpResult;
            ev.result = res.ok ? UiResult::Ok : UiResult::Failed;
            strncpy(ev.message, res.message, sizeof(ev.message) - 1);
        }

        if (ev.type != UiEvent::None) {
            _emit(ev, _emitUser);
        }
    }
}
