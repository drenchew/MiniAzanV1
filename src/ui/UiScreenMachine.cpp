#include "ui/UiScreenMachine.h"

void UiScreenMachine::reset(UiScreenId home) {
    _stack[0] = home;
    _depth = 0;
}

bool UiScreenMachine::push(UiScreenId screen) {
    if (_depth >= kMaxDepth - 1) return false;
    _depth++;
    _stack[_depth] = screen;
    return true;
}

bool UiScreenMachine::pop() {
    if (_depth <= 0) return false;
    _depth--;
    return true;
}

bool UiScreenMachine::replace(UiScreenId screen) {
    _stack[_depth] = screen;
    return true;
}
