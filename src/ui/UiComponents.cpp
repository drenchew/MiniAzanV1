#include "ui/UiComponents.h"
#include "ui/UiTheme.h"

#if defined(MINI_AZAN_UI_ENABLE) && MINI_AZAN_UI_ENABLE

lv_obj_t* UiComponents::createCard(lv_obj_t* parent, lv_coord_t w, lv_coord_t h) {
    lv_obj_t* c = lv_obj_create(parent);
    lv_obj_set_size(c, w, h);
    UiTheme::styleCard(c);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    return c;
}

UiStatusBarWidgets UiComponents::createStatusBar(lv_obj_t* parent, lv_coord_t width) {
    UiStatusBarWidgets sb{};
    sb.root = lv_obj_create(parent);
    lv_obj_set_size(sb.root, width, UiTheme::kStatusH);
    lv_obj_align(sb.root, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(sb.root, UiTheme::kSurface(), 0);
    lv_obj_set_style_bg_opa(sb.root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(sb.root, 0, 0);
    lv_obj_set_style_pad_hor(sb.root, 4, 0);
    lv_obj_set_flex_flow(sb.root, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(sb.root, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    auto mk = [&](const char* sym) {
        lv_obj_t* l = lv_label_create(sb.root);
        lv_label_set_text(l, sym);
        lv_obj_set_style_text_color(l, UiTheme::kMuted(), 0);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
        return l;
    };
    sb.wifi = mk(LV_SYMBOL_WIFI);
    sb.timeSrc = mk("RTC");
    sb.vol = mk(LV_SYMBOL_VOLUME_MAX);
    sb.sd = mk(LV_SYMBOL_SD_CARD);
    sb.batt = mk(LV_SYMBOL_BATTERY_FULL);
    return sb;
}

void UiComponents::updateStatusBar(UiStatusBarWidgets& sb, const UiEventPayload& ev) {
    if (!sb.root) return;
    if (sb.wifi) {
        lv_label_set_text(sb.wifi, ev.wifiOn ? LV_SYMBOL_WIFI : LV_SYMBOL_CLOSE);
        lv_obj_set_style_text_color(sb.wifi, ev.wifiOn ? UiTheme::kSuccess() : UiTheme::kMuted(), 0);
    }
    if (sb.timeSrc) lv_label_set_text(sb.timeSrc, ev.clockSource[0] ? ev.clockSource : ev.timeSource);
    if (sb.vol) {
        char b[8];
        snprintf(b, sizeof(b), "%u%%", (unsigned)ev.volumePct);
        lv_label_set_text(sb.vol, b);
    }
    if (sb.sd) {
        lv_label_set_text(sb.sd, ev.sdReady ? LV_SYMBOL_SD_CARD : LV_SYMBOL_WARNING);
        lv_obj_set_style_text_color(sb.sd, ev.sdReady ? UiTheme::kAccent() : UiTheme::kDanger(), 0);
    }
}

UiBottomNavWidgets UiComponents::createBottomNav(lv_obj_t* parent, UiScreenId active,
                                               void (*cb)(lv_event_t*)) {
    UiBottomNavWidgets nav{};
    nav.root = lv_obj_create(parent);
    lv_obj_set_size(nav.root, lv_pct(100), UiTheme::kNavH);
    lv_obj_align(nav.root, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(nav.root, UiTheme::kSurface(), 0);
    lv_obj_set_style_bg_opa(nav.root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(nav.root, 0, 0);
    lv_obj_set_style_radius(nav.root, 0, 0);
    lv_obj_set_flex_flow(nav.root, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(nav.root, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    static const char* labels[] = {"Home", "Prayer", "Azan", "Files", "System"};
    static const UiScreenId ids[] = {
        UiScreenId::Home, UiScreenId::PrayerTimes, UiScreenId::AzanSettings,
        UiScreenId::FileManager, UiScreenId::System};

    for (int i = 0; i < 5; i++) {
        lv_obj_t* b = lv_btn_create(nav.root);
        lv_obj_set_size(b, 44, 40);
        lv_obj_set_style_radius(b, 10, 0);
        lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, (void*)(uintptr_t)ids[i]);
        lv_obj_t* l = lv_label_create(b);
        lv_label_set_text(l, labels[i]);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
        lv_obj_center(l);
        nav.btns[i] = b;
    }
    highlightNav(nav, active);
    return nav;
}

void UiComponents::highlightNav(UiBottomNavWidgets& nav, UiScreenId active) {
    static const UiScreenId ids[] = {
        UiScreenId::Home, UiScreenId::PrayerTimes, UiScreenId::AzanSettings,
        UiScreenId::FileManager, UiScreenId::System};
    for (int i = 0; i < 5; i++) {
        if (!nav.btns[i]) continue;
        bool on = (ids[i] == active);
        lv_obj_set_style_bg_color(nav.btns[i], on ? UiTheme::kPrimary() : UiTheme::kCard(), 0);
        lv_obj_t* l = lv_obj_get_child(nav.btns[i], 0);
        if (l) lv_obj_set_style_text_color(l, on ? UiTheme::kText() : UiTheme::kMuted(), 0);
    }
}

lv_obj_t* UiComponents::createScrollContent(lv_obj_t* parent, lv_coord_t y, lv_coord_t h) {
    lv_obj_t* sc = lv_obj_create(parent);
    lv_obj_set_pos(sc, 0, y);
    lv_obj_set_size(sc, lv_pct(100), h);
    lv_obj_set_style_bg_opa(sc, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(sc, 0, 0);
    lv_obj_set_style_pad_all(sc, UiTheme::kPad, 0);
    lv_obj_set_flex_flow(sc, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(sc, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_add_flag(sc, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(sc, LV_SCROLLBAR_MODE_AUTO);
    return sc;
}

static lv_obj_t* g_dialog = nullptr;

void UiComponents::dismissDialog(lv_obj_t* dlg) {
    if (dlg) lv_obj_del(dlg);
    if (g_dialog == dlg) g_dialog = nullptr;
}

void UiComponents::showConfirmDialog(lv_obj_t* parent, const char* title, const char* msg,
                                     void (*onYes)(lv_event_t*), void* userData) {
    dismissDialog(g_dialog);
    g_dialog = lv_obj_create(parent);
    lv_obj_set_size(g_dialog, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(g_dialog, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_dialog, LV_OPA_60, 0);
    lv_obj_clear_flag(g_dialog, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* box = createCard(g_dialog, 200, 120);
    lv_obj_center(box);
    lv_obj_t* tl = lv_label_create(box);
    lv_label_set_text(tl, title);
    lv_obj_set_style_text_color(tl, UiTheme::kText(), 0);
    lv_obj_t* ml = lv_label_create(box);
    lv_label_set_text(ml, msg);
    lv_obj_set_width(ml, 180);
    lv_label_set_long_mode(ml, LV_LABEL_LONG_WRAP);
    lv_obj_align(ml, LV_ALIGN_TOP_LEFT, 0, 22);

    lv_obj_t* row = lv_obj_create(box);
    lv_obj_set_size(row, 180, 36);
    lv_obj_align(row, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* no = lv_btn_create(row);
    lv_obj_add_event_cb(no, [](lv_event_t* e) {
        dismissDialog(lv_obj_get_parent(lv_obj_get_parent(lv_event_get_target(e))));
    }, LV_EVENT_CLICKED, nullptr);
    lv_label_set_text(lv_label_create(no), "No");
    lv_obj_center(lv_obj_get_child(no, 0));

    lv_obj_t* yes = lv_btn_create(row);
    lv_obj_add_event_cb(yes, onYes, LV_EVENT_CLICKED, userData);
    lv_obj_set_style_bg_color(yes, UiTheme::kDanger(), 0);
    lv_label_set_text(lv_label_create(yes), "Yes");
    lv_obj_center(lv_obj_get_child(yes, 0));
}

#endif
