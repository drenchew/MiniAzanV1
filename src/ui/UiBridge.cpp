#include "ui/UiBridge.h"

bool UiBridge::begin() {
    if (_urgent && _cmd && _events) return true;
    _urgent = xQueueCreate(kUrgentQueueLen, sizeof(UiCommand));
    _cmd = xQueueCreate(kCmdQueueLen, sizeof(UiCommand));
    _events = xQueueCreate(kEventQueueLen, sizeof(UiEventPayload));
    return _urgent && _cmd && _events;
}

void UiBridge::end() {
    if (_urgent) { vQueueDelete(_urgent); _urgent = nullptr; }
    if (_cmd) { vQueueDelete(_cmd); _cmd = nullptr; }
    if (_events) { vQueueDelete(_events); _events = nullptr; }
}

bool UiBridge::postCommand(const UiCommand& cmd, TickType_t wait) {
    if (!_cmd) return false;
    return xQueueSend(_cmd, &cmd, wait) == pdTRUE;
}

bool UiBridge::postUrgent(const UiCommand& cmd, TickType_t wait) {
    if (!_urgent) return false;
    return xQueueSend(_urgent, &cmd, wait) == pdTRUE;
}

bool UiBridge::popUrgent(UiCommand& out, TickType_t wait) {
    if (!_urgent) return false;
    return xQueueReceive(_urgent, &out, wait) == pdTRUE;
}

bool UiBridge::popCommand(UiCommand& out, TickType_t wait) {
    if (!_cmd) return false;
    return xQueueReceive(_cmd, &out, wait) == pdTRUE;
}

bool UiBridge::postEvent(const UiEventPayload& ev, TickType_t wait) {
    if (!_events) return false;
    return xQueueSend(_events, &ev, wait) == pdTRUE;
}

bool UiBridge::popEvent(UiEventPayload& out, TickType_t wait) {
    if (!_events) return false;
    return xQueueReceive(_events, &out, wait) == pdTRUE;
}

uint32_t UiBridge::pendingCommands() const {
    if (!_cmd) return 0;
    return uxQueueMessagesWaiting(_cmd);
}

uint32_t UiBridge::pendingEvents() const {
    if (!_events) return 0;
    return uxQueueMessagesWaiting(_events);
}
