#pragma once

#include "ui/UiTypes.h"

/** Navigation stack for LVGL screens (UI task only). */
class UiScreenMachine {
public:
    static constexpr int kMaxDepth = 4;

    UiScreenId current() const { return _stack[_depth]; }
    int depth() const { return _depth; }

    void reset(UiScreenId home = UiScreenId::Home);
    bool push(UiScreenId screen);
    bool pop();
    bool replace(UiScreenId screen);

private:
    UiScreenId _stack[kMaxDepth]{UiScreenId::Home};
    int _depth = 0;
};
