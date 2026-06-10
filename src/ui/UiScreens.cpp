#include "ui/UiScreens.h"
#include "ui/UiTheme.h"
#include "UIManager.h"
#include "system/AzanSafeMode.h"
#include "AppLog.h"

#if defined(MINI_AZAN_UI_ENABLE) && MINI_AZAN_UI_ENABLE

static UiScreens* g_active = nullptr;

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

static void bridgePost(UiBridge* b, UiCommand cmd, bool urgent = false) {
    if (!b) return;
    if (urgent) b->postUrgent(cmd, 0);
    else        b->postCommand(cmd, 0);
}

// Encode a button operation tag into user-data pointer
static uintptr_t btnTag(uint8_t op, uint8_t arg = 0, uint8_t arg2 = 0) {
    return ((uintptr_t)op << 24) | ((uintptr_t)arg << 16) | ((uintptr_t)arg2 << 8);
}

// ─────────────────────────────────────────────────────────────────────────────
// Static LVGL callbacks
// ─────────────────────────────────────────────────────────────────────────────

/** Navigate to the screen encoded in user_data (used by menu list items). */
void UiScreens::lvCbNav(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED || !g_active) return;
    UiScreenId id = (UiScreenId)(uintptr_t)lv_event_get_user_data(e);
    g_active->show(id);
}

/** General button callback – op code in upper byte of user_data. */
void UiScreens::lvCbBtn(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    uintptr_t tag = (uintptr_t)lv_event_get_user_data(e);
    if (!g_active || !g_active->_bridge) return;
    UiBridge* b = g_active->_bridge;
    uint8_t op  = (uint8_t)(tag >> 24);

    // op=2  STOP AZAN (emergency, urgent)
    if (op == 2) {
        UIManager::requestStopAzan(*b);
        return;
    }
    // op=3  Go to Bluetooth screen (legacy – still works)
    if (op == 3) {
        g_active->show(UiScreenId::Bluetooth);
        return;
    }
    // op=4  Pause / Resume audio
    if (op == 4) {
        if (!g_active->_isAudioPlaying && !g_active->_isAudioPaused) {
            return;
        }
        UiCommand c{};
        c.cmd = g_active->_isAudioPaused ? UiCmd::ResumeAudio : UiCmd::PauseAudio;
        bridgePost(b, c);
        return;
    }
    // op=5  Up / parent-directory in QuranPlayer
    if (op == 5) {
        if (g_active->_screen == UiScreenId::QuranPlayer) {
            char parent[UI_PATH_MAX] = "/";
            const char* path = g_active->_currentBrowsePath;
            if (path && path[0] == '/' && path[1]) {
                const char* last = strrchr(path, '/');
                if (last && last != path) {
                    size_t len = (size_t)(last - path);
                    strncpy(parent, path, len);
                    parent[len] = '\0';
                }
            }
            g_active->requestFolderList(parent);
        }
        return;
    }
    // op=8/9  QuranPlayer previous / next page
    if (op == 8 || op == 9) {
        if (g_active->_screen == UiScreenId::QuranPlayer) {
            const uint8_t lastPage = g_active->_activeListTotal == 0
                ? 0
                : (uint8_t)((g_active->_activeListTotal - 1) / UI_FILE_PAGE_SIZE);
            if (op == 8 && g_active->_activeListPage > 0) {
                g_active->requestFolderPage(g_active->_currentBrowsePath,
                                            g_active->_activeListPage - 1);
            } else if (op == 9 && g_active->_activeListPage < lastPage) {
                g_active->requestFolderPage(g_active->_currentBrowsePath,
                                            g_active->_activeListPage + 1);
            }
        }
        return;
    }
    // op=6  Go to Menu
    if (op == 6) {
        g_active->show(UiScreenId::Menu);
        return;
    }
    // op=7  Go to Home
    if (op == 7) {
        g_active->show(UiScreenId::Home);
        return;
    }
    // op=10 Browse /azan
    if (op == 10) {
        g_active->requestFolderList("/azan");
        return;
    }
    // op=11 Browse /QuranRecitations/Luhaidan
    if (op == 11) {
        g_active->requestFolderList("/QuranRecitations/Luhaidan");
        return;
    }
    // op=12 Navigate to QuranPlayer at root
    if (op == 12) {
        strncpy(g_active->_currentBrowsePath, "/", sizeof(g_active->_currentBrowsePath) - 1);
        g_active->show(UiScreenId::QuranPlayer);
        return;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Initialisation
// ─────────────────────────────────────────────────────────────────────────────

void UiScreens::begin(UiBridge& bridge, UiScreenMachine& nav) {
    _bridge = &bridge;
    _nav    = &nav;
    g_active = this;

    _root = lv_obj_create(nullptr);
    UiTheme::styleScreen(_root);
    lv_obj_set_size(_root, 240, 320);

    // Status bar at top (22 px)
    _statusBar = UiComponents::createStatusBar(_root, 240);

    // Full-height content area below status bar – no bottom nav in new design
    lv_coord_t contentH = 320 - UiTheme::kStatusH;
    _contentArea = lv_obj_create(_root);
    lv_obj_set_pos(_contentArea, 0, UiTheme::kStatusH);
    lv_obj_set_size(_contentArea, 240, contentH);
    UiTheme::styleTransparent(_contentArea);

    lv_scr_load(_root);
    show(UiScreenId::Home);

    UiCommand c{};
    c.cmd = UiCmd::RequestSystemStatus; bridgePost(_bridge, c);
    c.cmd = UiCmd::RequestClock;        bridgePost(_bridge, c);
    c.cmd = UiCmd::RequestPrayerTimes;  bridgePost(_bridge, c);
}

void UiScreens::sendCmd(UiCommand cmd, bool urgent) {
    bridgePost(_bridge, cmd, urgent);
}

// ─────────────────────────────────────────────────────────────────────────────
// Navigation
// ─────────────────────────────────────────────────────────────────────────────

void UiScreens::show(UiScreenId id) {
    _screen = id;
    if (_nav) _nav->replace(id);
    rebuildShell(id);
    requestDataForScreen(id);
}

void UiScreens::rebuildShell(UiScreenId id) {
    lv_obj_clean(_contentArea);

    // Null all widget pointers
    _lblClock = _lblDate = _lblPrayerNow = _lblNextPrayer = nullptr;
    _arcSunPath = _lblArcFajr = _lblArcIsha = nullptr;
    _btnStopAzan = nullptr;
    _barProgress = _sliderVol = _swPreFajr = nullptr;
    _listFiles = _swTransfer = _lblSystem = _barBtProgress = _scroll = nullptr;
    _lblPageInfo = _btnPrevPage = _btnNextPage = nullptr;
    _lblNowPlaying = _btnPauseResume = _lblCurrentPath = _btnUpFolder = nullptr;
    _filePathCount = 0;
    for (int i = 0; i < 5; i++) {
        _prayerCards[i]      = nullptr;
        _prayerTimeLabels[i] = nullptr;
    }

    switch (id) {
        case UiScreenId::Home:          buildHome(_contentArea);          break;
        case UiScreenId::Menu:          buildMenu(_contentArea);          break;
        case UiScreenId::PrayerTimes:   buildPrayerTimes(_contentArea);   break;
        case UiScreenId::AzanSettings:  buildAzanSettings(_contentArea);  break;
        case UiScreenId::FileManager:   buildFileManager(_contentArea);   break;
        case UiScreenId::QuranPlayer:   buildQuranPlayer(_contentArea);   break;
        case UiScreenId::Bluetooth:     buildSystem(_contentArea);        break;
        default:                        buildHome(_contentArea);           break;
    }
}

void UiScreens::requestDataForScreen(UiScreenId id) {
    UiCommand c{};
    switch (id) {
        case UiScreenId::Home:
            c.cmd = UiCmd::RequestClock;       sendCmd(c);
            c.cmd = UiCmd::RequestPrayerTimes; sendCmd(c);
            break;
        case UiScreenId::Menu:
            // Status bar updates automatically via events; no specific request needed
            break;
        case UiScreenId::PrayerTimes:
            c.cmd = UiCmd::RequestPrayerTimes; sendCmd(c);
            break;
        case UiScreenId::AzanSettings:
            requestFolderList("/azan");
            c.cmd = UiCmd::RequestSystemStatus; sendCmd(c);
            break;
        case UiScreenId::FileManager:
            requestFolderList(_listFolder);
            break;
        case UiScreenId::QuranPlayer:
            requestFolderList(_currentBrowsePath);
            break;
        case UiScreenId::Bluetooth:
            c.cmd = UiCmd::RequestBluetoothStatus; sendCmd(c);
            c.cmd = UiCmd::RequestSystemStatus;    sendCmd(c);
            break;
        default: break;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Sub-screen header helper (back button + title)
// ─────────────────────────────────────────────────────────────────────────────

lv_obj_t* UiScreens::buildSubScreenHeader(lv_obj_t* area, const char* title) {
    lv_obj_t* hdr = lv_obj_create(area);
    lv_obj_set_size(hdr, 240, UiTheme::kSubHdrH);
    lv_obj_align(hdr, LV_ALIGN_TOP_MID, 0, 0);
    UiTheme::styleSubHeader(hdr);

    // Back button → Menu
    lv_obj_t* back = lv_btn_create(hdr);
    lv_obj_set_size(back, 52, 26);
    lv_obj_align(back, LV_ALIGN_LEFT_MID, 3, 0);
    lv_obj_set_style_bg_color(back, UiTheme::kCard(), 0);
    lv_obj_set_style_radius(back, 6, 0);
    lv_obj_set_style_shadow_width(back, 0, 0);
    lv_obj_t* bl = lv_label_create(back);
    lv_label_set_text(bl, LV_SYMBOL_LEFT " Back");
    lv_obj_set_style_text_font(bl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(bl, UiTheme::kText(), 0);
    lv_obj_center(bl);
    lv_obj_add_event_cb(back, lvCbBtn, LV_EVENT_CLICKED, (void*)btnTag(6));

    // Screen title
    lv_obj_t* lbl = lv_label_create(hdr);
    lv_label_set_text(lbl, title);
    lv_obj_set_style_text_color(lbl, UiTheme::kGold(), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);

    return hdr;
}

// ─────────────────────────────────────────────────────────────────────────────
// Sun-path arc update
// ─────────────────────────────────────────────────────────────────────────────

void UiScreens::updateSunArc() {
    if (!_arcSunPath) return;

    int fajrMin = (int)_lastPrayer.prayerMinutes[0]; // index 0 = Fajr
    int ishaMin = (int)_lastPrayer.prayerMinutes[5]; // index 5 = Isha
    int curMin  = _lastClock.hour * 60 + _lastClock.minute;

    // Update Fajr/Isha time labels
    static const char* kMonNames[] = {
        "Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
    if (_lblArcFajr && fajrMin > 0) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%02d:%02d", fajrMin / 60, fajrMin % 60);
        lv_label_set_text(_lblArcFajr, buf);
    }
    if (_lblArcIsha && ishaMin > 0) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%02d:%02d", ishaMin / 60, ishaMin % 60);
        lv_label_set_text(_lblArcIsha, buf);
    }

    // Compute arc value 0-100
    if (fajrMin <= 0 || ishaMin <= fajrMin) return;

    int pct;
    if (curMin <= fajrMin) {
        pct = 0;
    } else if (curMin >= ishaMin) {
        pct = 100;
    } else {
        pct = (curMin - fajrMin) * 100 / (ishaMin - fajrMin);
    }
    lv_arc_set_value(_arcSunPath, pct);
}

// ─────────────────────────────────────────────────────────────────────────────
// HOME SCREEN – Islamic dashboard
// ─────────────────────────────────────────────────────────────────────────────
//
//  Layout within content area (298 px tall, 240 px wide):
//  y=  0  Top bar   34px   [⚙ Menu]          [SD  BT%]
//  y= 34  Clock     36px        12:34:56
//  y= 70  Date      16px        7 Jun 2026  (gold)
//  y= 87  divider    1px   ─── gold line ───────────
//  y= 90  Arc      160px   sun-path half-circle (top half = y90..170)
//  y=166  Fajr/Isha labels near arc endpoints
//  y=176  Prayer card  62px   "Now: Dhuhr" / "Next: Asr in 01:24"
//  y=242  gap
//  y=258  STOP AZAN   36px  (BOTTOM_MID -4)
//
// ─────────────────────────────────────────────────────────────────────────────

void UiScreens::buildHome(lv_obj_t* area) {
    // ── Top bar ──────────────────────────────────────────────────────────────
    lv_obj_t* topBar = lv_obj_create(area);
    lv_obj_set_size(topBar, 240, 34);
    lv_obj_align(topBar, LV_ALIGN_TOP_MID, 0, 0);
    UiTheme::styleTransparent(topBar);

    // Gear / Menu button (top-left)
    lv_obj_t* gear = lv_btn_create(topBar);
    lv_obj_set_size(gear, 34, 30);
    lv_obj_align(gear, LV_ALIGN_LEFT_MID, 4, 0);
    lv_obj_set_style_bg_opa(gear, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_width(gear, 0, 0);
    lv_obj_set_style_border_width(gear, 0, 0);
    lv_obj_t* gearIco = lv_label_create(gear);
    lv_label_set_text(gearIco, LV_SYMBOL_SETTINGS);
    lv_obj_set_style_text_font(gearIco, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(gearIco, UiTheme::kGold(), 0);
    lv_obj_center(gearIco);
    lv_obj_add_event_cb(gear, lvCbBtn, LV_EVENT_CLICKED, (void*)btnTag(6));

    // // Battery placeholder (top-right)
    // lv_obj_t* battRow = lv_obj_create(topBar);
    // lv_obj_set_size(battRow, 70, 30);
    // lv_obj_align(battRow, LV_ALIGN_RIGHT_MID, -4, 0);
    // UiTheme::styleTransparent(battRow);
    // lv_obj_set_flex_flow(battRow, LV_FLEX_FLOW_ROW);
    // lv_obj_set_flex_align(battRow, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    // lv_obj_t* battIco = lv_label_create(battRow);
    // lv_label_set_text(battIco, LV_SYMBOL_BATTERY_FULL);
    // lv_obj_set_style_text_font(battIco, &lv_font_montserrat_14, 0);
    // lv_obj_set_style_text_color(battIco, UiTheme::kMuted(), 0);

    // ── Large clock (hero element) ────────────────────────────────────────
    _lblClock = lv_label_create(area);
    lv_label_set_text(_lblClock, "--:--:--");
    lv_obj_set_style_text_font(_lblClock, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(_lblClock, UiTheme::kText(), 0);
    lv_obj_align(_lblClock, LV_ALIGN_TOP_MID, 0, 36);

    // ── Date (gold, smaller) ──────────────────────────────────────────────
    _lblDate = lv_label_create(area);
    lv_label_set_text(_lblDate, "----/--/--");
    lv_obj_set_style_text_font(_lblDate, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_lblDate, UiTheme::kGold(), 0);
    lv_obj_align(_lblDate, LV_ALIGN_TOP_MID, 0, 70);

    // ── Thin gold divider ─────────────────────────────────────────────────
    lv_obj_t* div = lv_obj_create(area);
    lv_obj_set_size(div, 160, 1);
    lv_obj_align(div, LV_ALIGN_TOP_MID, 0, 88);
    lv_obj_set_style_bg_color(div, UiTheme::kGold(), 0);
    lv_obj_set_style_bg_opa(div, LV_OPA_40, 0);
    lv_obj_set_style_border_width(div, 0, 0);

    // ── Sun-path arc ──────────────────────────────────────────────────────
    // Object 160×160 centred horizontally; bounding-box top at y=91.
    // Draws a 180° half-circle from 180° (left/Fajr) clockwise to 360°/0° (right/Isha).
    // Only the top half is visible; the prayer card covers the lower bounding-box area.
    _arcSunPath = lv_arc_create(area);
    lv_obj_set_size(_arcSunPath, 160, 160);
    lv_obj_align(_arcSunPath, LV_ALIGN_TOP_MID, 0, 91);
    lv_arc_set_rotation(_arcSunPath, 0);
    lv_arc_set_bg_angles(_arcSunPath, 180, 360); // left → top → right  (half-circle)
    lv_arc_set_range(_arcSunPath, 0, 100);
    lv_arc_set_value(_arcSunPath, 0);
    lv_arc_set_mode(_arcSunPath, LV_ARC_MODE_NORMAL);
    lv_obj_clear_flag(_arcSunPath, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_pad_all(_arcSunPath, 0, 0);

    // Background track – dark arc ring
    lv_obj_set_style_arc_color(_arcSunPath, UiTheme::kArcBg(), LV_PART_MAIN);
    lv_obj_set_style_arc_width(_arcSunPath, 6, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_arcSunPath, LV_OPA_TRANSP, LV_PART_MAIN);

    // Indicator (filled portion) – Islamic gold
    lv_obj_set_style_arc_color(_arcSunPath, UiTheme::kGold(), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(_arcSunPath, 6, LV_PART_INDICATOR);

    // Knob – bright sun dot
    lv_obj_set_style_bg_color(_arcSunPath, UiTheme::kGoldBright(), LV_PART_KNOB);
    lv_obj_set_style_bg_opa(_arcSunPath, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_pad_all(_arcSunPath, 5, LV_PART_KNOB);
    lv_obj_set_style_border_width(_arcSunPath, 0, LV_PART_KNOB);

    // ── Fajr / Isha labels (near arc endpoints at y≈171) ─────────────────
    _lblArcFajr = lv_label_create(area);
    lv_label_set_text(_lblArcFajr, "Fajr");
    lv_obj_set_style_text_font(_lblArcFajr, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_lblArcFajr, UiTheme::kMuted(), 0);
    lv_obj_set_pos(_lblArcFajr, 8, 174);

    _lblArcIsha = lv_label_create(area);
    lv_label_set_text(_lblArcIsha, "Isha");
    lv_obj_set_style_text_font(_lblArcIsha, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_lblArcIsha, UiTheme::kMuted(), 0);
    lv_obj_set_pos(_lblArcIsha, 200, 174);

    // ── Prayer info card ──────────────────────────────────────────────────
    lv_obj_t* pCard = UiComponents::createCard(area, 224, 62);
    lv_obj_align(pCard, LV_ALIGN_TOP_MID, 0, 200); // todo: adjust y based on arc size
    // Left gold accent border
    lv_obj_set_style_border_side(pCard, LV_BORDER_SIDE_LEFT, 0);
    lv_obj_set_style_border_color(pCard, UiTheme::kGold(), 0);
    lv_obj_set_style_border_width(pCard, 3, 0);

    _lblPrayerNow = lv_label_create(pCard);
    lv_label_set_text(_lblPrayerNow, "Loading...");
    lv_obj_set_style_text_font(_lblPrayerNow, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_lblPrayerNow, UiTheme::kGold(), 0);
    lv_obj_align(_lblPrayerNow, LV_ALIGN_TOP_LEFT, 0, 0);

    _lblNextPrayer = lv_label_create(pCard);
    lv_label_set_text(_lblNextPrayer, "Next: --");
    lv_obj_set_style_text_font(_lblNextPrayer, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_lblNextPrayer, UiTheme::kText(), 0);
    lv_obj_align(_lblNextPrayer, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    // ── STOP AZAN (emergency – always reachable) ──────────────────────────
    _btnStopAzan = lv_btn_create(area);
    lv_obj_set_size(_btnStopAzan, 224, 36);
    lv_obj_align(_btnStopAzan, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_obj_set_style_bg_color(_btnStopAzan, UiTheme::kDanger(), 0);
    lv_obj_set_style_radius(_btnStopAzan, 8, 0);
    lv_obj_set_style_shadow_width(_btnStopAzan, 0, 0);
    lv_obj_add_event_cb(_btnStopAzan, lvCbBtn, LV_EVENT_CLICKED, (void*)btnTag(2));
    if (!_isAzanPlaying) {
        lv_obj_add_flag(_btnStopAzan, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_t* sl = lv_label_create(_btnStopAzan);
    lv_label_set_text(sl, LV_SYMBOL_STOP " STOP AZAN");
    lv_obj_set_style_text_font(sl, &lv_font_montserrat_14, 0);
    lv_obj_center(sl);
}

// ─────────────────────────────────────────────────────────────────────────────
// MENU SCREEN
// ─────────────────────────────────────────────────────────────────────────────

void UiScreens::buildMenu(lv_obj_t* area) {
    // Header with back-to-Home button
    lv_obj_t* hdr = lv_obj_create(area);
    lv_obj_set_size(hdr, 240, 40);
    lv_obj_align(hdr, LV_ALIGN_TOP_MID, 0, 0);
    UiTheme::styleSubHeader(hdr);

    lv_obj_t* backBtn = lv_btn_create(hdr);
    lv_obj_set_size(backBtn, 52, 28);
    lv_obj_align(backBtn, LV_ALIGN_LEFT_MID, 3, 0);
    lv_obj_set_style_bg_color(backBtn, UiTheme::kCard(), 0);
    lv_obj_set_style_radius(backBtn, 6, 0);
    lv_obj_set_style_shadow_width(backBtn, 0, 0);
    lv_obj_t* backLbl = lv_label_create(backBtn);
    lv_label_set_text(backLbl, LV_SYMBOL_LEFT " Home");
    lv_obj_set_style_text_font(backLbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(backLbl, UiTheme::kText(), 0);
    lv_obj_center(backLbl);
    lv_obj_add_event_cb(backBtn, lvCbBtn, LV_EVENT_CLICKED, (void*)btnTag(7)); // op=7 = Home

    lv_obj_t* title = lv_label_create(hdr);
    lv_label_set_text(title, LV_SYMBOL_SETTINGS "  Menu");
    lv_obj_set_style_text_color(title, UiTheme::kGold(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

    // Menu list
    lv_coord_t listH = lv_obj_get_height(area) - 40;
    lv_obj_t* list = lv_list_create(area);
    lv_obj_set_size(list, 240, listH);
    lv_obj_align(list, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(list, UiTheme::kBg(), 0);
    lv_obj_set_style_bg_opa(list, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_row(list, 4, 0);
    lv_obj_set_style_pad_all(list, 6, 0);

    // Helper to add and style a menu row
    auto addRow = [&](const char* icon, const char* label, UiScreenId sid) {
        lv_obj_t* btn = lv_list_add_btn(list, icon, label);
        lv_obj_set_height(btn, 44);
        lv_obj_set_style_bg_color(btn, UiTheme::kCard(), 0);
        lv_obj_set_style_bg_color(btn, UiTheme::kSurface(), LV_STATE_PRESSED);
        lv_obj_set_style_radius(btn, 10, 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        // Style the text label inside the list button (child 1 = text)
        lv_obj_t* lbl = lv_obj_get_child(btn, 1);
        if (lbl) {
            lv_obj_set_style_text_color(lbl, UiTheme::kText(), 0);
            lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
        }
        // Style the icon label (child 0)
        lv_obj_t* ico = lv_obj_get_child(btn, 0);
        if (ico) {
            lv_obj_set_style_text_color(ico, UiTheme::kGold(), 0);
            lv_obj_set_style_text_font(ico, &lv_font_montserrat_20, 0);
        }
        lv_obj_add_event_cb(btn, lvCbNav, LV_EVENT_CLICKED, (void*)(uintptr_t)sid);
    };

    addRow(LV_SYMBOL_BELL,      "Prayer Times",   UiScreenId::PrayerTimes);
    addRow(LV_SYMBOL_AUDIO,     "Azan Settings",  UiScreenId::AzanSettings);
    addRow(LV_SYMBOL_PLAY,      "Quran Player",   UiScreenId::QuranPlayer);
    addRow(LV_SYMBOL_DIRECTORY, "File Manager",   UiScreenId::FileManager);
    addRow(LV_SYMBOL_BLUETOOTH, "Bluetooth",      UiScreenId::Bluetooth);
}

// ─────────────────────────────────────────────────────────────────────────────
// PRAYER TIMES SCREEN
// ─────────────────────────────────────────────────────────────────────────────

static const char* kPrayerLabels[]    = {"Fajr", "Sunrise", "Dhuhr", "Asr", "Maghrib", "Isha"};
static const int   kPrayerDisplayIdx[] = {0, 2, 3, 4, 5};

void UiScreens::buildPrayerTimes(lv_obj_t* area) {
    buildSubScreenHeader(area, "Prayer Times");
    int remainH = lv_obj_get_height(area) - UiTheme::kSubHdrH;
    _scroll = UiComponents::createScrollContent(area, UiTheme::kSubHdrH, remainH);

    for (int k = 0; k < 5; k++) {
        int idx = kPrayerDisplayIdx[k];
        lv_obj_t* card = UiComponents::createCard(_scroll, 216, 52);
        _prayerCards[k] = card;
        lv_obj_t* nm = lv_label_create(card);
        lv_label_set_text(nm, kPrayerLabels[idx]);
        lv_obj_set_style_text_color(nm, UiTheme::kText(), 0);
        lv_obj_t* tm = lv_label_create(card);
        lv_label_set_text(tm, "--:--");
        lv_obj_set_style_text_color(tm, UiTheme::kGold(), 0);
        lv_obj_align(tm, LV_ALIGN_TOP_RIGHT, 0, 0);
        _prayerTimeLabels[k] = tm;
    }

    lv_obj_t* ref = lv_btn_create(_scroll);
    lv_obj_set_width(ref, 216);
    lv_obj_set_style_bg_color(ref, UiTheme::kCard(), 0);
    lv_obj_add_event_cb(ref, [](lv_event_t*) {
        if (!g_active) return;
        UiCommand c{};
        c.cmd = UiCmd::RequestPrayerTimes;
        g_active->sendCmd(c);
    }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* rl = lv_label_create(ref);
    lv_label_set_text(rl, LV_SYMBOL_REFRESH " Refresh");
    lv_obj_set_style_text_color(rl, UiTheme::kText(), 0);
    lv_obj_center(rl);
}

// ─────────────────────────────────────────────────────────────────────────────
// AZAN SETTINGS SCREEN
// ─────────────────────────────────────────────────────────────────────────────

void UiScreens::buildAzanSettings(lv_obj_t* area) {
    buildSubScreenHeader(area, "Azan Settings");
    int remainH = lv_obj_get_height(area) - UiTheme::kSubHdrH;
    _scroll = UiComponents::createScrollContent(area, UiTheme::kSubHdrH, remainH);

    // Pre-Fajr alarm card
    lv_obj_t* card1 = UiComponents::createCard(_scroll, 216, 70);
    lv_obj_t* pl = lv_label_create(card1);
    lv_label_set_text(pl, "Pre-Fajr alarm");
    lv_obj_set_style_text_color(pl, UiTheme::kText(), 0);
    _swPreFajr = lv_switch_create(card1);
    lv_obj_align(_swPreFajr, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_add_event_cb(_swPreFajr, [](lv_event_t* e) {
        if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED || !g_active) return;
        UiCommand c{};
        c.cmd = UiCmd::SetPreFajr;
        c.pf.enabled = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
        g_active->sendCmd(c);
    }, LV_EVENT_VALUE_CHANGED, nullptr);

    // Volume card
    lv_obj_t* card2 = UiComponents::createCard(_scroll, 216, 80);
    lv_obj_t* vl = lv_label_create(card2);
    lv_label_set_text(vl, "Volume");
    lv_obj_set_style_text_color(vl, UiTheme::kText(), 0);
    _sliderVol = lv_slider_create(card2);
    lv_slider_set_range(_sliderVol, 0, 100);
    lv_obj_set_width(_sliderVol, 190);
    lv_obj_align(_sliderVol, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(_sliderVol, UiTheme::kGold(), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(_sliderVol, UiTheme::kGold(), LV_PART_KNOB);
    lv_obj_add_event_cb(_sliderVol, [](lv_event_t* e) {
        if (lv_event_get_code(e) != LV_EVENT_RELEASED || !g_active) return;
        UiCommand c{};
        c.cmd = UiCmd::SetVolume;
        c.vol.volumePct = (uint8_t)lv_slider_get_value(lv_event_get_target(e));
        g_active->sendCmd(c);
    }, LV_EVENT_RELEASED, nullptr);

    // File list card
    lv_obj_t* card3 = UiComponents::createCard(_scroll, 216, 120);
    lv_obj_t* fl = lv_label_create(card3);
    lv_label_set_text(fl, "Default Azan (SD /azan)");
    lv_obj_set_style_text_color(fl, UiTheme::kText(), 0);
    _listFiles = lv_list_create(card3);
    lv_obj_set_size(_listFiles, 200, 88);
    lv_obj_align(_listFiles, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(_listFiles, UiTheme::kBg(), 0);
    lv_obj_set_style_border_width(_listFiles, 0, 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// FILE MANAGER SCREEN
// ─────────────────────────────────────────────────────────────────────────────

void UiScreens::buildFileManager(lv_obj_t* area) {
    buildSubScreenHeader(area, "File Manager");
    int y0 = UiTheme::kSubHdrH;

    // Tab row (azan / quran / player)
    lv_obj_t* tabs = lv_obj_create(area);
    lv_obj_set_pos(tabs, 0, y0);
    lv_obj_set_size(tabs, 240, 36);
    lv_obj_set_style_bg_opa(tabs, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(tabs, 0, 0);
    lv_obj_set_style_pad_all(tabs, 2, 0);
    lv_obj_set_flex_flow(tabs, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(tabs, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(tabs, LV_OBJ_FLAG_SCROLLABLE);

    auto addTab = [&](const char* lbl, uint8_t op) {
        lv_obj_t* b = lv_btn_create(tabs);
        lv_obj_set_style_bg_color(b, UiTheme::kCard(), 0);
        lv_obj_set_style_radius(b, 6, 0);
        lv_label_set_text(lv_label_create(b), lbl);
        lv_obj_center(lv_obj_get_child(b, 0));
        lv_obj_add_event_cb(b, lvCbBtn, LV_EVENT_CLICKED, (void*)btnTag(op));
    };
    addTab("/azan",  10);
    addTab("Quran",  11);
    addTab("Player", 12);

    lv_coord_t listH = lv_obj_get_height(area) - y0 - 40;
    _listFiles = lv_list_create(area);
    lv_obj_set_pos(_listFiles, 0, y0 + 40);
    lv_obj_set_size(_listFiles, 240, listH);
    lv_obj_set_style_bg_color(_listFiles, UiTheme::kBg(), 0);
    lv_obj_set_style_border_width(_listFiles, 0, 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// QURAN PLAYER SCREEN
// ─────────────────────────────────────────────────────────────────────────────

void UiScreens::buildQuranPlayer(lv_obj_t* area) {
    buildSubScreenHeader(area, "Quran Player");
    int y0 = UiTheme::kSubHdrH;

    lv_obj_t* info = lv_obj_create(area);
    lv_obj_set_pos(info, 8, y0 + 2);
    lv_obj_set_size(info, 224, 72);
    UiTheme::styleTransparent(info);
    lv_obj_set_flex_flow(info, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(info, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(info, 2, 0);

    // Current path
    _lblCurrentPath = lv_label_create(info);
    lv_label_set_text(_lblCurrentPath, "Path: /");
    lv_label_set_long_mode(_lblCurrentPath, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(_lblCurrentPath, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_lblCurrentPath, UiTheme::kMuted(), 0);
    lv_obj_set_width(_lblCurrentPath, 220);
    lv_obj_set_height(_lblCurrentPath, 18);

    // Now-playing label
    _lblNowPlaying = lv_label_create(info);
    lv_label_set_text(_lblNowPlaying, "No file playing");
    lv_label_set_long_mode(_lblNowPlaying, LV_LABEL_LONG_DOT);
    lv_obj_set_width(_lblNowPlaying, 220);
    lv_obj_set_height(_lblNowPlaying, 18);
    lv_obj_set_style_text_font(_lblNowPlaying, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_lblNowPlaying, UiTheme::kGold(), 0);

    _lblPageInfo = lv_label_create(info);
    lv_label_set_text(_lblPageInfo, "Page 1");
    lv_label_set_long_mode(_lblPageInfo, LV_LABEL_LONG_DOT);
    lv_obj_set_width(_lblPageInfo, 220);
    lv_obj_set_height(_lblPageInfo, 16);
    lv_obj_set_style_text_font(_lblPageInfo, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_lblPageInfo, UiTheme::kMuted(), 0);

    // File list
    lv_coord_t listY = y0 + 78;
    lv_coord_t listH = lv_obj_get_height(area) - listY - 40;
    _listFiles = lv_list_create(area);
    lv_obj_set_pos(_listFiles, 0, listY);
    lv_obj_set_size(_listFiles, 240, listH);
    lv_obj_set_style_bg_color(_listFiles, UiTheme::kBg(), 0);
    lv_obj_set_style_border_width(_listFiles, 0, 0);

    // Playback control row at bottom
    lv_obj_t* row = lv_obj_create(area);
    lv_obj_set_size(row, 240, 40);
    lv_obj_align(row, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    _btnUpFolder = lv_btn_create(row);
    lv_obj_set_size(_btnUpFolder, 34, 34);
    lv_obj_set_style_bg_color(_btnUpFolder, UiTheme::kCard(), 0);
    lv_label_set_text(lv_label_create(_btnUpFolder), LV_SYMBOL_UP);
    lv_obj_center(lv_obj_get_child(_btnUpFolder, 0));
    lv_obj_add_event_cb(_btnUpFolder, lvCbBtn, LV_EVENT_CLICKED, (void*)btnTag(5));

    _btnPrevPage = lv_btn_create(row);
    lv_obj_set_size(_btnPrevPage, 34, 34);
    lv_obj_set_style_bg_color(_btnPrevPage, UiTheme::kCard(), 0);
    lv_label_set_text(lv_label_create(_btnPrevPage), "<");
    lv_obj_center(lv_obj_get_child(_btnPrevPage, 0));
    lv_obj_add_event_cb(_btnPrevPage, lvCbBtn, LV_EVENT_CLICKED, (void*)btnTag(8));

    _btnPauseResume = lv_btn_create(row);
    lv_obj_set_size(_btnPauseResume, 34, 34);
    lv_obj_set_style_bg_color(_btnPauseResume, UiTheme::kCard(), 0);
    lv_label_set_text(lv_label_create(_btnPauseResume), LV_SYMBOL_PAUSE);
    lv_obj_center(lv_obj_get_child(_btnPauseResume, 0));
    lv_obj_add_event_cb(_btnPauseResume, lvCbBtn, LV_EVENT_CLICKED, (void*)btnTag(4));
    if (_isAudioPlaying) {
        lv_label_set_text(lv_obj_get_child(_btnPauseResume, 0), LV_SYMBOL_PAUSE);
    } else if (_isAudioPaused) {
        lv_label_set_text(lv_obj_get_child(_btnPauseResume, 0), LV_SYMBOL_PLAY);
    } else {
        lv_label_set_text(lv_obj_get_child(_btnPauseResume, 0), LV_SYMBOL_PLAY);
        lv_obj_add_state(_btnPauseResume, LV_STATE_DISABLED);
    }

    _btnNextPage = lv_btn_create(row);
    lv_obj_set_size(_btnNextPage, 34, 34);
    lv_obj_set_style_bg_color(_btnNextPage, UiTheme::kCard(), 0);
    lv_label_set_text(lv_label_create(_btnNextPage), ">");
    lv_obj_center(lv_obj_get_child(_btnNextPage, 0));
    lv_obj_add_event_cb(_btnNextPage, lvCbBtn, LV_EVENT_CLICKED, (void*)btnTag(9));

    lv_obj_t* stop = lv_btn_create(row);
    lv_obj_set_size(stop, 34, 34);
    lv_obj_set_style_bg_color(stop, UiTheme::kDanger(), 0);
    lv_label_set_text(lv_label_create(stop), LV_SYMBOL_STOP);
    lv_obj_center(lv_obj_get_child(stop, 0));
    lv_obj_add_event_cb(stop, lvCbBtn, LV_EVENT_CLICKED, (void*)btnTag(2));

    updateQuranPager();
}

// ─────────────────────────────────────────────────────────────────────────────
// BLUETOOTH SCREEN
// ─────────────────────────────────────────────────────────────────────────────

void UiScreens::buildSystem(lv_obj_t* area) {
    buildSubScreenHeader(area, "Bluetooth");
    int remainH = lv_obj_get_height(area) - UiTheme::kSubHdrH;
    _scroll = UiComponents::createScrollContent(area, UiTheme::kSubHdrH, remainH);

    // Audio streaming section
    lv_obj_t* titleAudio = lv_label_create(_scroll);
    lv_label_set_text(titleAudio, "Bluetooth Audio Streaming");
    lv_obj_set_style_text_color(titleAudio, UiTheme::kGold(), 0);

    lv_obj_t* cardAudio = UiComponents::createCard(_scroll, 216, 100);
    lv_obj_t* lblAudioDesc = lv_label_create(cardAudio);
    lv_label_set_text(lblAudioDesc, "Stream audio from\nphone to speaker");
    lv_obj_set_style_text_color(lblAudioDesc, UiTheme::kText(), 0);
    lv_obj_set_width(lblAudioDesc, 200);

    lv_obj_t* lblAudioSw = lv_label_create(cardAudio);
    lv_label_set_text(lblAudioSw, "Enable");
    lv_obj_set_style_text_color(lblAudioSw, UiTheme::kMuted(), 0);
    lv_obj_align(lblAudioSw, LV_ALIGN_BOTTOM_LEFT, 0, -8);
    lv_obj_t* swAudio = lv_switch_create(cardAudio);
    lv_obj_align(swAudio, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_add_event_cb(swAudio, [](lv_event_t* e) {
        if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
        UiCommand c{};
        c.cmd = UiCmd::ToggleBluetoothStreaming;
        if (g_active) g_active->sendCmd(c);
    }, LV_EVENT_VALUE_CHANGED, nullptr);

    // File transfer section
    lv_obj_t* titleXfer = lv_label_create(_scroll);
    lv_label_set_text(titleXfer, "Bluetooth File Transfer");
    lv_obj_set_style_text_color(titleXfer, UiTheme::kGold(), 0);

    lv_obj_t* card = UiComponents::createCard(_scroll, 216, 160);
    _lblSystem = lv_label_create(card);
    lv_label_set_text(_lblSystem, "Transfer mode: OFF\nStatus: Offline\n(SPP not implemented)");
    lv_label_set_long_mode(_lblSystem, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(_lblSystem, 200);
    lv_obj_set_style_text_color(_lblSystem, UiTheme::kText(), 0);

    lv_obj_t* lblSw = lv_label_create(card);
    lv_label_set_text(lblSw, "Transfer Mode");
    lv_obj_set_style_text_color(lblSw, UiTheme::kMuted(), 0);
    lv_obj_align(lblSw, LV_ALIGN_BOTTOM_LEFT, 0, -8);
    _swTransfer = lv_switch_create(card);
    lv_obj_align(_swTransfer, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_add_event_cb(_swTransfer, [](lv_event_t* e) {
        if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
        UiCommand c{};
        c.cmd = UiCmd::ToggleTransferMode;
        if (g_active) g_active->sendCmd(c);
    }, LV_EVENT_VALUE_CHANGED, nullptr);

    lv_obj_t* progCard = UiComponents::createCard(_scroll, 216, 56);
    lv_obj_t* progLbl = lv_label_create(progCard);
    lv_label_set_text(progLbl, "Upload progress");
    lv_obj_set_style_text_color(progLbl, UiTheme::kMuted(), 0);
    _barBtProgress = lv_bar_create(progCard);
    lv_obj_set_size(_barBtProgress, 200, 12);
    lv_obj_align(_barBtProgress, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_bar_set_range(_barBtProgress, 0, 100);
    lv_bar_set_value(_barBtProgress, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(_barBtProgress, UiTheme::kGold(), LV_PART_INDICATOR);
}

// ─────────────────────────────────────────────────────────────────────────────
// Folder-list helpers
// ─────────────────────────────────────────────────────────────────────────────

void UiScreens::requestFolderList(const char* path) {
    requestFolderPage(path, 0);
}

void UiScreens::requestFolderPage(const char* path, uint8_t page) {
    const char* folder = (path && path[0]) ? path : "/";
    _listRequestSeq++;
    if (_listRequestSeq == 0) _listRequestSeq = 1;
    _activeListRequestId = _listRequestSeq;
    _activeListPage = page;
    strncpy(_activeListFolder, folder, sizeof(_activeListFolder) - 1);
    _activeListFolder[sizeof(_activeListFolder) - 1] = '\0';

    if (_screen == UiScreenId::QuranPlayer) {
        strncpy(_currentBrowsePath, folder, sizeof(_currentBrowsePath) - 1);
        _currentBrowsePath[sizeof(_currentBrowsePath) - 1] = '\0';
        if (_lblCurrentPath) {
            char pb[80];
            snprintf(pb, sizeof(pb), "Path: %s", _currentBrowsePath);
            lv_label_set_text(_lblCurrentPath, pb);
        }
        if (_lblPageInfo) {
            char pageBuf[32];
            snprintf(pageBuf, sizeof(pageBuf), "Loading page %u...", (unsigned)page + 1);
            lv_label_set_text(_lblPageInfo, pageBuf);
        }
    } else {
        strncpy(_listFolder, folder, sizeof(_listFolder) - 1);
        _listFolder[sizeof(_listFolder) - 1] = '\0';
    }

    _streamCount    = 0;
    _filePathCount  = 0;
    if (_listFiles) lv_obj_clean(_listFiles);

    UiCommand c{};
    c.cmd          = UiCmd::ListFolder;
    strncpy(c.list.path, folder, sizeof(c.list.path) - 1);
    c.list.page      = page;
    c.list.requestId = _activeListRequestId;
    sendCmd(c);
}

bool UiScreens::isCurrentListResult(const UiEventPayload& ev) const {
    if (ev.listRequestId == 0 || ev.listRequestId != _activeListRequestId) return false;
    if (strncmp(ev.listFolder, _activeListFolder, sizeof(_activeListFolder)) != 0) return false;
    return _screen == UiScreenId::QuranPlayer
        || _screen == UiScreenId::FileManager
        || _screen == UiScreenId::AzanSettings;
}

void UiScreens::formatCountdown(char* buf, size_t len, int seconds) {
    if (seconds < 0) { snprintf(buf, len, "--:--:--"); return; }
    int h = seconds / 3600;
    int m = (seconds % 3600) / 60;
    int s = seconds % 60;
    snprintf(buf, len, "%02d:%02d:%02d", h, m, s);
}

void UiScreens::updateQuranPager() {
    if (_screen != UiScreenId::QuranPlayer) {
        return;
    }

    const uint8_t lastPage = _activeListTotal == 0
        ? 0
        : (uint8_t)((_activeListTotal - 1) / UI_FILE_PAGE_SIZE);

    if (_lblPageInfo) {
        char pageBuf[40];
        if (_activeListTotal == 0) {
            snprintf(pageBuf, sizeof(pageBuf), "Page %u", (unsigned)_activeListPage + 1);
        } else {
            snprintf(pageBuf, sizeof(pageBuf), "Page %u/%u  (%u files)",
                     (unsigned)_activeListPage + 1,
                     (unsigned)lastPage + 1,
                     (unsigned)_activeListTotal);
        }
        lv_label_set_text(_lblPageInfo, pageBuf);
    }

    if (_btnPrevPage) {
        if (_activeListPage == 0) lv_obj_add_state(_btnPrevPage, LV_STATE_DISABLED);
        else                      lv_obj_clear_state(_btnPrevPage, LV_STATE_DISABLED);
    }
    if (_btnNextPage) {
        if (_activeListPage >= lastPage) lv_obj_add_state(_btnNextPage, LV_STATE_DISABLED);
        else                             lv_obj_clear_state(_btnNextPage, LV_STATE_DISABLED);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// populateFileList – fills _listFiles widget from a FileListReady event
// ─────────────────────────────────────────────────────────────────────────────

void UiScreens::populateFileList(const UiEventPayload& ev) {
    if (!isCurrentListResult(ev)) {
        appLogf(2, "UI", "drop stale FileListReady id=%lu active=%lu",
                (unsigned long)ev.listRequestId, (unsigned long)_activeListRequestId);
        return;
    }
    if (!_listFiles) return;

    appLogf(2, "UI", "populateFileList: page=%u files=%d total=%u in %s",
            (unsigned)ev.listPage, ev.fileCount, (unsigned)ev.listTotal,
            ev.listFolder[0] ? ev.listFolder : _listFolder);

    lv_obj_clean(_listFiles);
    _filePathCount = ev.fileCount > UI_FILE_PAGE_SIZE ? UI_FILE_PAGE_SIZE : ev.fileCount;
    _activeListPage = ev.listPage;
    _activeListTotal = ev.listTotal;

    const char* folder = ev.listFolder[0] ? ev.listFolder : _listFolder;

    if (_screen == UiScreenId::QuranPlayer) {
        strncpy(_currentBrowsePath, folder, sizeof(_currentBrowsePath) - 1);
        _currentBrowsePath[sizeof(_currentBrowsePath) - 1] = '\0';
        if (_lblCurrentPath) {
            char pb[80];
            snprintf(pb, sizeof(pb), "Path: %s", folder);
            lv_label_set_text(_lblCurrentPath, pb);
        }
        updateQuranPager();
    }

    for (uint8_t i = 0; i < _filePathCount; i++) {
        if (folder[0] == '/' && folder[1]) {
            snprintf(_filePaths[i], sizeof(_filePaths[i]), "%s/%s", folder, ev.files[i].name);
        } else {
            snprintf(_filePaths[i], sizeof(_filePaths[i]), "/%s", ev.files[i].name);
        }
        _isFolder[i] = ev.files[i].isFolder;

        const char* icon = ev.files[i].isFolder ? LV_SYMBOL_DIRECTORY : LV_SYMBOL_AUDIO;
        lv_obj_t* btn = lv_list_add_btn(_listFiles, icon, ev.files[i].name);

        lv_obj_add_event_cb(btn, [](lv_event_t* e) {
            if (lv_event_get_code(e) != LV_EVENT_CLICKED || !g_active) return;
            int idx = (int)(intptr_t)lv_event_get_user_data(e);
            if (idx < 0 || idx >= g_active->_filePathCount) return;

            if (g_active->_isFolder[idx]) {
                if (g_active->_screen == UiScreenId::QuranPlayer)
                    g_active->requestFolderList(g_active->_filePaths[idx]);
                return;
            }

            UiCommand c{};
            if (g_active->_screen == UiScreenId::AzanSettings) {
                c.cmd = UiCmd::SelectAzanFile;
                strncpy(c.azanPath.path, g_active->_filePaths[idx], sizeof(c.azanPath.path) - 1);
            } else {
                c.cmd = UiCmd::PlayFile;
                strncpy(c.play.path, g_active->_filePaths[idx], sizeof(c.play.path) - 1);
                strncpy(g_active->_currentPlayingPath, g_active->_filePaths[idx],
                        sizeof(g_active->_currentPlayingPath) - 1);
                if (g_active->_lblNowPlaying) {
                    char buf[80];
                    const char* dn = g_active->_filePaths[idx];
                    const char* sl = strrchr(dn, '/');
                    if (sl) dn = sl + 1;
                    snprintf(buf, sizeof(buf), "Playing: %s", dn);
                    lv_label_set_text(g_active->_lblNowPlaying, buf);
                }
            }
            g_active->sendCmd(c);
        }, LV_EVENT_CLICKED, (void*)(intptr_t)i);

        if (_screen == UiScreenId::FileManager) {
            lv_obj_add_event_cb(btn, [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_LONG_PRESSED || !g_active) return;
                int idx = (int)(intptr_t)lv_event_get_user_data(e);
                if (idx < 0 || idx >= g_active->_filePathCount) return;
                strncpy(g_active->_pendingDelete, g_active->_filePaths[idx],
                        sizeof(g_active->_pendingDelete) - 1);
                UiComponents::showConfirmDialog(g_active->_root, "Delete file?",
                    g_active->_pendingDelete,
                    [](lv_event_t* ev2) {
                        if (!g_active) return;
                        UiCommand c{};
                        c.cmd = UiCmd::DeleteFile;
                        strncpy(c.del.path, g_active->_pendingDelete, sizeof(c.del.path) - 1);
                        g_active->sendCmd(c);
                        UiComponents::dismissDialog(lv_obj_get_parent(
                            lv_obj_get_parent(lv_event_get_target(ev2))));
                    }, nullptr);
            }, LV_EVENT_LONG_PRESSED, (void*)(intptr_t)i);
        }
    }

    lv_obj_invalidate(_listFiles);
    appLogf(2, "UI", "populateFileList done: %d page entries", _filePathCount);
}

// ─────────────────────────────────────────────────────────────────────────────
// onEvent – handles all system → UI events
// ─────────────────────────────────────────────────────────────────────────────

void UiScreens::onEvent(const UiEventPayload& ev) {
    char buf[80];
    UiComponents::updateStatusBar(_statusBar, ev);

    switch (ev.type) {

        case UiEvent::ClockUpdate:
            _lastClock = ev;
            if (_lblClock) {
                snprintf(buf, sizeof(buf), "%02d:%02d:%02d", ev.hour, ev.minute, ev.second);
                lv_label_set_text(_lblClock, buf);
            }
            if (_lblDate) {
                static const char* kMon[] = {
                    "Jan","Feb","Mar","Apr","May","Jun",
                    "Jul","Aug","Sep","Oct","Nov","Dec"};
                int mi = ev.month - 1;
                if (mi < 0 || mi > 11) mi = 0;
                snprintf(buf, sizeof(buf), "%d %s %04d", ev.mday, kMon[mi], ev.year);
                lv_label_set_text(_lblDate, buf);
            }
            if (_screen == UiScreenId::Home) updateSunArc();
            break;

        case UiEvent::PrayerTimesUpdate:
            _lastPrayer = ev;
            if (_lblPrayerNow && _screen == UiScreenId::Home) {
                if (ev.currentPrayerIndex >= 0)
                    snprintf(buf, sizeof(buf), "Now: %s", ev.currentPrayerName);
                else
                    snprintf(buf, sizeof(buf), "Between prayers");
                lv_label_set_text(_lblPrayerNow, buf);
            }
            if (_lblNextPrayer && ev.nextPrayerIndex >= 0) {
                char cd[16];
                formatCountdown(cd, sizeof(cd), ev.secondsToNext);
                snprintf(buf, sizeof(buf), "Next: %s in %s", ev.nextPrayerName, cd);
                lv_label_set_text(_lblNextPrayer, buf);
            }
            if (_screen == UiScreenId::Home) updateSunArc();
            if (_screen == UiScreenId::PrayerTimes) {
                for (int k = 0; k < 5; k++) {
                    int idx = kPrayerDisplayIdx[k];
                    if (_prayerTimeLabels[k]) {
                        int mm = ev.prayerMinutes[idx];
                        snprintf(buf, sizeof(buf), "%02d:%02d", mm / 60, mm % 60);
                        lv_label_set_text(_prayerTimeLabels[k], buf);
                    }
                    if (_prayerCards[k]) {
                        bool highlight = (idx == ev.currentPrayerIndex || idx == ev.nextPrayerIndex);
                        lv_obj_set_style_border_color(_prayerCards[k],
                            highlight ? UiTheme::kGold() : lv_color_hex(0x2A3547), 0);
                        lv_obj_set_style_border_width(_prayerCards[k], highlight ? 2 : 1, 0);
                    }
                }
            }
            break;

        case UiEvent::BluetoothStatus:
            if (_swTransfer) {
                if (ev.btEnabled) lv_obj_add_state(_swTransfer, LV_STATE_CHECKED);
                else              lv_obj_clear_state(_swTransfer, LV_STATE_CHECKED);
            }
            if (_barBtProgress)
                lv_bar_set_value(_barBtProgress, ev.btProgressPct, LV_ANIM_OFF);
            if (_lblSystem && _screen == UiScreenId::Bluetooth) {
                snprintf(buf, sizeof(buf), "Transfer: %s\nConnected: %s\n%s",
                         ev.btEnabled ? "ON" : "OFF",
                         ev.btConnected ? "yes" : "no",
                         ev.btStatusMsg[0] ? ev.btStatusMsg : "Ready");
                lv_label_set_text(_lblSystem, buf);
            }
            break;

        case UiEvent::SystemStatus:
            if (_sliderVol)  lv_slider_set_value(_sliderVol, ev.volumePct, LV_ANIM_OFF);
            if (_swPreFajr) {
                if (ev.preFajr) lv_obj_add_state(_swPreFajr, LV_STATE_CHECKED);
                else            lv_obj_clear_state(_swPreFajr, LV_STATE_CHECKED);
            }
            if (_lblSystem && _screen == UiScreenId::Bluetooth) {
                snprintf(buf, sizeof(buf), "SD: %s\nRTC: %s\nDefault: %s",
                         ev.sdReady ? "OK" : "FAIL", ev.timeSource,
                         ev.defaultAzanPath[0] ? ev.defaultAzanPath : "(built-in)");
                lv_label_set_text(_lblSystem, buf);
            }
            UiComponents::updateStatusBar(_statusBar, ev);
            break;

        case UiEvent::VolumeState:
            if (_sliderVol) lv_slider_set_value(_sliderVol, ev.volumePct, LV_ANIM_OFF);
            break;

        case UiEvent::PreFajrState:
            if (_swPreFajr) {
                if (ev.preFajr) lv_obj_add_state(_swPreFajr, LV_STATE_CHECKED);
                else            lv_obj_clear_state(_swPreFajr, LV_STATE_CHECKED);
            }
            break;

        case UiEvent::FileListStreamStart:
            if (!isCurrentListResult(ev)) break;
            _streamCount = 0;
            if (_listFiles) lv_obj_clean(_listFiles);
            break;

        case UiEvent::FileListStreamEntry:
            if (!isCurrentListResult(ev)) break;
            if (_streamCount < UI_FILE_PAGE_SIZE && ev.fileCount > 0)
                _streamFiles[_streamCount++] = ev.files[0];
            break;

        case UiEvent::FileListStreamEnd:
            if (!isCurrentListResult(ev)) break;
            break;

        case UiEvent::FileListReady:
            populateFileList(ev);
            break;

        case UiEvent::StorageBusy:
            break;

        case UiEvent::AudioState:
            _isAudioPlaying = ev.audioPlaying;
            _isAudioPaused = ev.audioPaused;
            _isAzanPlaying = ev.azanPlaying;
            if (_btnStopAzan) {
                if (_isAzanPlaying) {
                    lv_obj_clear_flag(_btnStopAzan, LV_OBJ_FLAG_HIDDEN);
                } else {
                    lv_obj_add_flag(_btnStopAzan, LV_OBJ_FLAG_HIDDEN);
                }
            }
            if (_screen == UiScreenId::QuranPlayer && _btnPauseResume) {
                if (_isAudioPlaying) {
                    lv_obj_clear_state(_btnPauseResume, LV_STATE_DISABLED);
                    lv_label_set_text(lv_obj_get_child(_btnPauseResume, 0), LV_SYMBOL_PAUSE);
                } else if (_isAudioPaused) {
                    lv_obj_clear_state(_btnPauseResume, LV_STATE_DISABLED);
                    lv_label_set_text(lv_obj_get_child(_btnPauseResume, 0), LV_SYMBOL_PLAY);
                } else {
                    lv_obj_add_state(_btnPauseResume, LV_STATE_DISABLED);
                    lv_label_set_text(lv_obj_get_child(_btnPauseResume, 0), LV_SYMBOL_PLAY);
                }
            }
            break;

        case UiEvent::FileOpResult:
            if (_screen == UiScreenId::FileManager)
                requestFolderList(_listFolder);
            break;

        default:
            break;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// tickRefresh – called from UIManager's periodic task (≈1 s on Home screen)
// ─────────────────────────────────────────────────────────────────────────────

void UiScreens::tickRefresh() {
    if (!AzanSafeMode::allowLvglTickRefresh()) return;
    uint32_t now = millis();
    if (now - _lastRefreshMs < 1000) return;
    _lastRefreshMs = now;
    if (_screen != UiScreenId::Home) return;
    UiCommand c{};
    c.cmd = UiCmd::RequestClock;       sendCmd(c);
    c.cmd = UiCmd::RequestPrayerTimes; sendCmd(c);
}

#endif
