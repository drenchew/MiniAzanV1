#pragma once

#include <Arduino.h>
#include "freertos/queue.h"
#include "StorageManager.h"
#include "ui/UiTypes.h"

/**
 * Cooperative SD worker (state machine). One incremental step per tick().
 * Never calls LVGL or UiBridge directly — results drained in poll().
 */
class StorageJobQueue {
public:
    enum class JobType : uint8_t {
        ListDir = 0,
        DeleteFile,
    };

    enum class StreamPhase : uint8_t {
        Idle = 0,
        ListStart,
        ListEntry,
        ListEnd,
        DeleteRun,
        Paused,
    };

    struct Job {
        JobType type = JobType::ListDir;
        char path[UI_PATH_MAX]{"/"};
        uint8_t page = 0;
        uint8_t pageSize = UI_FILE_PAGE_SIZE;
        uint32_t requestId = 0;
    };

    struct JobResult {
        JobType type = JobType::ListDir;
        StreamPhase phase = StreamPhase::Idle;
        uint32_t requestId = 0;
        bool ok = false;
        char folder[UI_PATH_MAX]{};
        uint8_t fileCount = 0;
        uint8_t listTotal = 0;
        uint8_t listPage = 0;
        UiFileEntry entry{};
        UiFileEntry files[UI_FILE_PAGE_SIZE]{};
        char message[UI_PATH_MAX]{};
    };

    using EmitFn = void (*)(const UiEventPayload& ev, void* user);
    using LogFn = void (*)(int level, const char* tag, const char* message);

    static constexpr uint32_t kWorkerStackWords = 3072;

    bool begin(StorageManager* storage, EmitFn emit, void* user, LogFn log = nullptr);
    bool submit(const Job& job);
    void invalidateBefore(uint32_t requestId);
    bool isBusy() const { return _phase != StreamPhase::Idle; }

    /** One cooperative step (SysCoord P4). Returns true if work remains. */
    bool tick();

    /** Drain result queue → UiEvents (SysCoord only). */
    void poll();

private:
    void startJob(const Job& job);
    bool tickList();
    bool tickDelete();
    void pushResult(const JobResult& res);
    void finishList(bool ok);
    void logMsg(int level, const char* msg) const;
    void logf(int level, const char* fmt, ...) const;

    StorageManager* _storage = nullptr;
    EmitFn _emit = nullptr;
    void* _emitUser = nullptr;
    LogFn _log = nullptr;
    QueueHandle_t _jobQ = nullptr;
    QueueHandle_t _resultQ = nullptr;

    Job _active{};
    StreamPhase _phase = StreamPhase::Idle;
    int _listCursor = 0;
    int _listTotal = 0;
    uint8_t _batchCount = 0;
    UiFileEntry _batch[UI_FILE_PAGE_SIZE]{};
    uint32_t _nextId = 1;
    uint32_t _minValidRequestId = 1;
};
