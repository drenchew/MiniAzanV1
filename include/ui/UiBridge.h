#pragma once

#include "ui/UiTypes.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

/**
 * Thread-safe command/event bus between LVGL task and AppCoordinator.
 * No SD, audio, or network calls here.
 */
class UiBridge {
public:
    static constexpr UBaseType_t kCmdQueueLen = 16;
    static constexpr UBaseType_t kUrgentQueueLen = 4;
    static constexpr UBaseType_t kEventQueueLen = 16;

    bool begin();
    void end();

    bool postCommand(const UiCommand& cmd, TickType_t wait = 0);
    bool postUrgent(const UiCommand& cmd, TickType_t wait = 0);

    bool popUrgent(UiCommand& out, TickType_t wait = 0);
    bool popCommand(UiCommand& out, TickType_t wait = 0);

    bool postEvent(const UiEventPayload& ev, TickType_t wait = 0);
    bool popEvent(UiEventPayload& out, TickType_t wait = 0);

    uint32_t pendingCommands() const;
    uint32_t pendingEvents() const;

private:
    QueueHandle_t _urgent = nullptr;
    QueueHandle_t _cmd = nullptr;
    QueueHandle_t _events = nullptr;
};
