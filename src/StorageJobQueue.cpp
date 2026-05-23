#include "StorageJobQueue.h"
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
        _resultQ = xQueueCreate(4, sizeof(JobResult));
    }
    if (!_jobQ || !_resultQ) {
        logMsg(0, "queue create failed");
        return false;
    }

    if (!_task) {
        BaseType_t ok = xTaskCreatePinnedToCore(
            workerEntry,
            "SDJob",
            kWorkerStackWords,
            this,
            2,
            &_task,
            0);
        if (ok != pdPASS) {
            logMsg(0, "task create failed");
            return false;
        }
    }

    logf(2, "SDJob ready stack=%lu words", (unsigned long)kWorkerStackWords);
    return _storage != nullptr && _emit != nullptr;
}

bool StorageJobQueue::submit(const Job& job) {
    if (!_jobQ) return false;
    Job copy = job;
    if (copy.requestId == 0) {
        copy.requestId = _nextId++;
    }

    const bool ok = xQueueSend(_jobQ, &copy, 0) == pdTRUE;
    if (ok) {
        logf(2, "submit id=%lu type=%u path=%s page=%u",
             (unsigned long)copy.requestId,
             (unsigned)copy.type,
             copy.path,
             (unsigned)copy.page);
    } else {
        logf(1, "submit REJECTED queue full type=%u path=%s",
             (unsigned)copy.type, copy.path);
    }
    return ok;
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
    if (xQueueSend(_resultQ, &res, 0) != pdTRUE) {
        logf(1, "result queue full id=%lu", (unsigned long)res.requestId);
    }
}

void StorageJobQueue::poll() {
    if (!_resultQ || !_emit) return;

    JobResult res{};
    while (xQueueReceive(_resultQ, &res, 0) == pdTRUE) {
        UiEventPayload ev{};
        if (res.type == JobType::ListDir) {
            ev.type = UiEvent::FileListReady;
            ev.result = res.ok ? UiResult::Ok : UiResult::Failed;
            ev.fileCount = res.fileCount;
            ev.listPage = res.listPage;
            ev.listTotal = res.listTotal;
            ev.listRequestId = res.requestId;
            strncpy(ev.listFolder, res.folder, sizeof(ev.listFolder) - 1);
            for (uint8_t i = 0; i < res.fileCount && i < 16; i++) {
                ev.files[i] = res.files[i];
            }
        } else if (res.type == JobType::DeleteFile) {
            ev.type = UiEvent::FileOpResult;
            ev.result = res.ok ? UiResult::Ok : UiResult::Failed;
            strncpy(ev.message, res.message, sizeof(ev.message) - 1);
        }
        _emit(ev, _emitUser);
    }
}

void StorageJobQueue::workerEntry(void* arg) {
    static_cast<StorageJobQueue*>(arg)->workerLoop();
}

bool StorageJobQueue::runListJob(const Job& job, JobResult& out) {
    out = {};
    out.type = JobType::ListDir;
    out.requestId = job.requestId;
    out.listPage = job.page;
    strncpy(out.folder, job.path, sizeof(out.folder) - 1);

    if (!_storage || !_storage->isReady()) {
        return false;
    }
    if (_storage->isPlaybackLocked()) {
        return false;
    }

    StorageManager::DirEntry entries[16]{};
    int total = 0;
    const int skip = (int)job.page * (int)job.pageSize;
    const int n = _storage->listDirectoryPage(
        job.path, entries, (int)job.pageSize, skip, &total);

    if (n < 0) {
        return false;
    }

    out.fileCount = (uint8_t)n;
    out.listTotal = (uint8_t)(total > 255 ? 255 : total);
    for (uint8_t i = 0; i < out.fileCount; i++) {
        strncpy(out.files[i].name, entries[i].name, sizeof(out.files[i].name) - 1);
        out.files[i].size = entries[i].size;
    }
    return true;
}

bool StorageJobQueue::runDeleteJob(const Job& job, JobResult& out) {
    out = {};
    out.type = JobType::DeleteFile;
    out.requestId = job.requestId;
    strncpy(out.message, job.path, sizeof(out.message) - 1);

    if (!_storage || !job.path[0]) {
        return false;
    }
    if (_storage->isPlaybackLocked()) {
        return false;
    }
    return _storage->removeFile(job.path);
}

void StorageJobQueue::processJob(const Job& job) {
    const uint32_t t0 = millis();
    JobResult res{};

    logf(2, "run id=%lu type=%u path=%s",
         (unsigned long)job.requestId, (unsigned)job.type, job.path);

    if (job.type == JobType::ListDir) {
        res.ok = runListJob(job, res);
    } else if (job.type == JobType::DeleteFile) {
        res.ok = runDeleteJob(job, res);
    }

    pushResult(res);

    logf(2, "done id=%lu ok=%d ms=%lu files=%u",
         (unsigned long)job.requestId,
         res.ok ? 1 : 0,
         (unsigned long)(millis() - t0),
         (unsigned)res.fileCount);
}

void StorageJobQueue::workerLoop() {
    Job job{};
    for (;;) {
        if (xQueueReceive(_jobQ, &job, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        _busy = true;
        processJob(job);
        _busy = false;
    }
}
