#pragma once

#if defined(MINI_AZAN_UI_ENABLE) && MINI_AZAN_UI_ENABLE

#include <lvgl.h>

namespace UiTheme {

inline lv_color_t col(uint32_t hex) { return lv_color_hex(hex); }

// ── Core palette ──────────────────────────────────────────────────────────
inline lv_color_t kBg()          { return col(0x0A0E14); } // deep navy-black
inline lv_color_t kSurface()     { return col(0x131920); } // elevated surface
inline lv_color_t kCard()        { return col(0x1B2330); } // card background
inline lv_color_t kPrimary()     { return col(0x1f6feb); } // interactive blue
inline lv_color_t kAccent()      { return col(0x58a6ff); } // bright blue accent
inline lv_color_t kText()        { return col(0xE8EDF3); } // primary text
inline lv_color_t kMuted()       { return col(0x6E7B8B); } // secondary/muted
inline lv_color_t kDanger()      { return col(0xC0392B); } // danger red
inline lv_color_t kSuccess()     { return col(0x238636); } // success green

// ── Islamic theme colours ─────────────────────────────────────────────────
inline lv_color_t kGold()        { return col(0xC8A96E); } // Islamic gold
inline lv_color_t kGoldBright()  { return col(0xFFD060); } // sun dot / highlight
inline lv_color_t kIslamGreen()  { return col(0x1E8B6E); } // deep teal-green
inline lv_color_t kArcBg()       { return col(0x1A2535); } // arc track background
inline lv_color_t kHeaderA()     { return col(0x162038); } // gradient top
inline lv_color_t kHeaderB()     { return col(0x0A0E14); } // gradient bottom

// ── Layout constants ──────────────────────────────────────────────────────
static const int kStatusH  = 22;  // status bar height
static const int kSubHdrH  = 34;  // sub-screen back-button header height
static const int kPad      = 8;
static const int kRadius   = 12;
// kNavH kept at 0 – no bottom nav in the new design
static const int kNavH     = 0;

// ── Style helpers ─────────────────────────────────────────────────────────
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
    lv_obj_set_style_border_color(obj, col(0x2A3547), 0);
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

// Sub-screen back-button header bar (kSubHdrH tall)
inline void styleSubHeader(lv_obj_t* obj) {
    lv_obj_set_style_bg_color(obj, kSurface(), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_side(obj, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(obj, kGold(), 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

// Transparent container (no bg, no border)
inline void styleTransparent(lv_obj_t* obj) {
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

} // namespace UiTheme

#endif
