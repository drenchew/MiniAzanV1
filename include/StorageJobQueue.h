#pragma once

#include <Arduino.h>
#include "freertos/queue.h"
#include "StorageManager.h"
#include "ui/UiTypes.h"

/**
 * Async SD worker (SDJob task). Never calls LVGL or UiBridge directly.
 * Results are queued and delivered via poll() on the main/coordinator thread.
 */
class StorageJobQueue {
public:
    enum class JobType : uint8_t {
        ListDir = 0,
        DeleteFile,
    };

    struct Job {
        JobType type = JobType::ListDir;
        char path[64]{"/"};
        uint8_t page = 0;
        uint8_t pageSize = 16;
        uint32_t requestId = 0;
    };

    /** Compact result posted from SDJob → drained in poll(). */
    struct JobResult {
        JobType type = JobType::ListDir;
        uint32_t requestId = 0;
        bool ok = false;
        char folder[64]{};
        uint8_t fileCount = 0;
        uint8_t listTotal = 0;
        uint8_t listPage = 0;
        UiFileEntry files[16]{};
        char message[64]{};
    };

    using EmitFn = void (*)(const UiEventPayload& ev, void* user);
    using LogFn = void (*)(int level, const char* tag, const char* message);

    static constexpr uint32_t kWorkerStackWords = 4096;  /**< ~16 KB on ESP32 */

    bool begin(StorageManager* storage, EmitFn emit, void* user, LogFn log = nullptr);
    bool submit(const Job& job);
    bool isBusy() const { return _busy; }

    /** Call from main loop / AppCoordinator only — posts UiEvents. */
    void poll();

private:
    static void workerEntry(void* arg);
    void workerLoop();
    void processJob(const Job& job);
    bool runListJob(const Job& job, JobResult& out);
    bool runDeleteJob(const Job& job, JobResult& out);
    void pushResult(const JobResult& res);
    void logMsg(int level, const char* msg) const;
    void logf(int level, const char* fmt, ...) const;

    StorageManager* _storage = nullptr;
    EmitFn _emit = nullptr;
    void* _emitUser = nullptr;
    LogFn _log = nullptr;
    QueueHandle_t _jobQ = nullptr;
    QueueHandle_t _resultQ = nullptr;
    TaskHandle_t _task = nullptr;
    volatile bool _busy = false;
    uint32_t _nextId = 1;
};
