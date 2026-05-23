#pragma once

#if defined(MINI_AZAN_UI_ENABLE) && MINI_AZAN_UI_ENABLE

#include <lvgl.h>

namespace UiTheme {

inline lv_color_t col(uint32_t hex) { return lv_color_hex(hex); }

inline lv_color_t kBg()       { return col(0x0d1117); }
inline lv_color_t kSurface()  { return col(0x161b22); }
inline lv_color_t kCard()     { return col(0x21262d); }
inline lv_color_t kPrimary()  { return col(0x1f6feb); }
inline lv_color_t kAccent()   { return col(0x58a6ff); }
inline lv_color_t kText()     { return col(0xe6edf3); }
inline lv_color_t kMuted()    { return col(0x8b949e); }
inline lv_color_t kDanger()   { return col(0xda3633); }
inline lv_color_t kSuccess()  { return col(0x238636); }
inline lv_color_t kHeaderA()  { return col(0x1a3a5c); }
inline lv_color_t kHeaderB()  { return col(0x0d2137); }

static const int kStatusH = 22;
static const int kNavH = 48;
static const int kPad = 8;
static const int kRadius = 12;

inline void styleScreen(lv_obj_t* obj) {
    lv_obj_set_style_bg_color(obj, kBg(), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
}

inline void styleCard(lv_obj_t* obj) {
    lv_obj_set_style_bg_color(obj, kCard(), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(obj, kRadius, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(0x30363d), 0);
    lv_obj_set_style_shadow_width(obj, 8, 0);
    lv_obj_set_style_shadow_opa(obj, LV_OPA_30, 0);
    lv_obj_set_style_pad_all(obj, kPad, 0);
}

inline void styleHeaderGradient(lv_obj_t* obj) {
    lv_obj_set_style_bg_color(obj, kHeaderA(), 0);
    lv_obj_set_style_bg_grad_color(obj, kHeaderB(), 0);
    lv_obj_set_style_bg_grad_dir(obj, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, 0, 0);
}

} // namespace UiTheme

#endif
