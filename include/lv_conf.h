/**
 * LVGL 8 — MiniAzan (ILI9341 240x320)
 */
#if defined(MINI_AZAN_UI_ENABLE) && MINI_AZAN_UI_ENABLE

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

#define LV_COLOR_DEPTH     16
#define LV_COLOR_16_SWAP   0

#define LV_MEM_CUSTOM      0
#define LV_MEM_SIZE        (28U * 1024U)
#define LV_MEM_BUF_MAX_NUM 8

#define LV_USE_LOG         0
#define LV_USE_ASSERT_NULL 0
#define LV_USE_ASSERT_MALLOC 0

#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_DEFAULT        &lv_font_montserrat_14

#define LV_USE_LABEL    1
#define LV_USE_BTN      1
#define LV_USE_BTNMATRIX 1
#define LV_USE_SLIDER   1
#define LV_USE_LIST     1
#define LV_USE_SWITCH   1
#define LV_USE_BAR      1
#define LV_USE_FLEX     1
#define LV_USE_ARC      0
#define LV_USE_CHART    0
#define LV_USE_TABLE    0
#define LV_USE_TABVIEW  0
#define LV_USE_TILEVIEW 0
#define LV_USE_WIN      0
#define LV_USE_SPAN     0
#define LV_USE_IMGBTN   0
#define LV_USE_MSGBOX   0
#define LV_USE_TEXTAREA 0
#define LV_USE_SPINBOX  0
#define LV_USE_EXTRA    0
#define LV_USE_SPINNER  0
#define LV_USE_CALENDAR 0
#define LV_USE_KEYBOARD 0
#define LV_USE_DROPDOWN 0
#define LV_USE_ROLLER   0
#define LV_USE_ANIMIMG  0

#define LV_TICK_CUSTOM         1
#define LV_TICK_CUSTOM_INCLUDE "Arduino.h"
#define LV_TICK_CUSTOM_SYS_TIME_EXPR (millis())

#define LV_DISP_DEF_REFR_PERIOD 30
#define LV_INDEV_DEF_READ_PERIOD 20

#endif /* LV_CONF_H */
#endif
