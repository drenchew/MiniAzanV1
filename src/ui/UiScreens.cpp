#include "ui/UiScreens.h"
#include "ui/UiTheme.h"
#include "UIManager.h"
#include "system/AzanSafeMode.h"

#if defined(MINI_AZAN_UI_ENABLE) && MINI_AZAN_UI_ENABLE

static UiScreens* g_active = nullptr;

static void bridgePost(UiBridge* b, UiCommand cmd, bool urgent = false) {
    if (!b) return;
    if (urgent) b->postUrgent(cmd, 0);
    else b->postCommand(cmd, 0);
}

static uintptr_t btnTag(uint8_t op, uint8_t arg = 0, uint8_t arg2 = 0) {
    return ((uintptr_t)op << 24) | ((uintptr_t)arg << 16) | ((uintptr_t)arg2 << 8);
}

void UiScreens::lvCbNav(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED || !g_active) return;
    UiScreenId id = (UiScreenId)(uintptr_t)lv_event_get_user_data(e);
    g_active->show(id);
}

void UiScreens::lvCbBtn(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    uintptr_t tag = (uintptr_t)lv_event_get_user_data(e);
    if (!g_active || !g_active->_bridge) return;
    UiBridge* b = g_active->_bridge;
    uint8_t op = (uint8_t)(tag >> 24);

    if (op == 2) {
        UIManager::requestStopAzan(*b);
        return;
    }
    if (op == 3) {
        g_active->show(UiScreenId::Bluetooth);
        return;
    }
    if (op == 10) {
        UiCommand c{};
        c.cmd = UiCmd::ListFolder;
        strncpy(c.list.path, "/azan", sizeof(c.list.path) - 1);
        strncpy(g_active->_listFolder, "/azan", sizeof(g_active->_listFolder) - 1);
        bridgePost(b, c);
        return;
    }
    if (op == 11) {
        UiCommand c{};
        c.cmd = UiCmd::ListFolder;
        strncpy(c.list.path, "/quran", sizeof(c.list.path) - 1);
        strncpy(g_active->_listFolder, "/quran", sizeof(g_active->_listFolder) - 1);
        bridgePost(b, c);
        return;
    }
    if (op == 12) {
        g_active->show(UiScreenId::QuranPlayer);
        return;
    }
}

void UiScreens::begin(UiBridge& bridge, UiScreenMachine& nav) {
    _bridge = &bridge;
    _nav = &nav;
    g_active = this;

    _root = lv_obj_create(nullptr);
    UiTheme::styleScreen(_root);
    lv_obj_set_size(_root, 240, 320);

    _statusBar = UiComponents::createStatusBar(_root, 240);
    _bottomNav = UiComponents::createBottomNav(_root, UiScreenId::Home, lvCbNav);

    lv_coord_t contentH = 320 - UiTheme::kStatusH - UiTheme::kNavH;
    _contentArea = lv_obj_create(_root);
    lv_obj_set_pos(_contentArea, 0, UiTheme::kStatusH);
    lv_obj_set_size(_contentArea, 240, contentH);
    lv_obj_set_style_bg_opa(_contentArea, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(_contentArea, 0, 0);
    lv_obj_set_style_pad_all(_contentArea, 0, 0);

    lv_scr_load(_root);
    show(UiScreenId::Home);

    UiCommand c{};
    c.cmd = UiCmd::RequestSystemStatus;
    bridgePost(_bridge, c);
    c.cmd = UiCmd::RequestClock;
    bridgePost(_bridge, c);
    c.cmd = UiCmd::RequestPrayerTimes;
    bridgePost(_bridge, c);
}

void UiScreens::sendCmd(UiCommand cmd, bool urgent) {
    bridgePost(_bridge, cmd, urgent);
}

void UiScreens::show(UiScreenId id) {
    _screen = id;
    if (_nav) _nav->replace(id);
    rebuildShell(id);
    UiComponents::highlightNav(_bottomNav, id);
    requestDataForScreen(id);
}

void UiScreens::rebuildShell(UiScreenId id) {
    lv_obj_clean(_contentArea);
    _lblClock = _lblDate = _lblPrayerNow = _lblNextPrayer = nullptr;
    _barProgress = _sliderVol = _swPreFajr = _listFiles = _swTransfer = _lblSystem = _barBtProgress = _scroll = nullptr;
    _filePathCount = 0;
    for (int i = 0; i < 5; i++) {
        _prayerCards[i] = nullptr;
        _prayerTimeLabels[i] = nullptr;
    }

    switch (id) {
        case UiScreenId::Home: buildHome(_contentArea); break;
        case UiScreenId::PrayerTimes: buildPrayerTimes(_contentArea); break;
        case UiScreenId::AzanSettings: buildAzanSettings(_contentArea); break;
        case UiScreenId::FileManager: buildFileManager(_contentArea); break;
        case UiScreenId::QuranPlayer: buildQuranPlayer(_contentArea); break;
        case UiScreenId::Bluetooth: buildSystem(_contentArea); break;
        default: buildHome(_contentArea); break;
    }
}

void UiScreens::requestDataForScreen(UiScreenId id) {
    UiCommand c{};
    switch (id) {
        case UiScreenId::Home:
            c.cmd = UiCmd::RequestClock;
            sendCmd(c);
            c.cmd = UiCmd::RequestPrayerTimes;
            sendCmd(c);
            break;
        case UiScreenId::PrayerTimes:
            c.cmd = UiCmd::RequestPrayerTimes;
            sendCmd(c);
            break;
        case UiScreenId::AzanSettings:
            c.cmd = UiCmd::ListAudioFiles;
            sendCmd(c);
            c.cmd = UiCmd::RequestSystemStatus;
            sendCmd(c);
            break;
        case UiScreenId::FileManager:
            c.cmd = UiCmd::ListFolder;
            strncpy(c.list.path, _listFolder, sizeof(c.list.path) - 1);
            sendCmd(c);
            break;
        case UiScreenId::QuranPlayer:
            c.cmd = UiCmd::ListFolder;
            strncpy(c.list.path, "/quran", sizeof(c.list.path) - 1);
            sendCmd(c);
            break;
        case UiScreenId::Bluetooth:
            c.cmd = UiCmd::RequestBluetoothStatus;
            sendCmd(c);
            c.cmd = UiCmd::RequestSystemStatus;
            sendCmd(c);
            break;
        default: break;
    }
}

void UiScreens::formatCountdown(char* buf, size_t len, int seconds) {
    if (seconds < 0) {
        snprintf(buf, len, "--:--:--");
        return;
    }
    int h = seconds / 3600;
    int m = (seconds % 3600) / 60;
    int s = seconds % 60;
    snprintf(buf, len, "%02d:%02d:%02d", h, m, s);
}

void UiScreens::buildHome(lv_obj_t* area) {
    lv_obj_t* hdr = lv_obj_create(area);
    lv_obj_set_size(hdr, lv_pct(100), 100);
    lv_obj_align(hdr, LV_ALIGN_TOP_MID, 0, 0);
    UiTheme::styleHeaderGradient(hdr);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    _lblClock = lv_label_create(hdr);
    lv_label_set_text(_lblClock, "--:--");
    lv_obj_set_style_text_font(_lblClock, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(_lblClock, UiTheme::kText(), 0);
    lv_obj_align(_lblClock, LV_ALIGN_TOP_MID, 0, 12);

    _lblDate = lv_label_create(hdr);
    lv_label_set_text(_lblDate, "----/--/--");
    lv_obj_set_style_text_color(_lblDate, UiTheme::kMuted(), 0);
    lv_obj_align(_lblDate, LV_ALIGN_TOP_MID, 0, 40);

    lv_obj_t* card = UiComponents::createCard(area, 224, 100);
    lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 108);

    _lblPrayerNow = lv_label_create(card);
    lv_label_set_text(_lblPrayerNow, "Loading...");
    lv_obj_set_style_text_color(_lblPrayerNow, UiTheme::kAccent(), 0);

    _lblNextPrayer = lv_label_create(card);
    lv_label_set_text(_lblNextPrayer, "Next: --");
    lv_obj_align(_lblNextPrayer, LV_ALIGN_TOP_LEFT, 0, 28);
    lv_obj_set_style_text_color(_lblNextPrayer, UiTheme::kText(), 0);

    _barProgress = lv_bar_create(card);
    lv_obj_set_size(_barProgress, 200, 10);
    lv_obj_align(_barProgress, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_bar_set_range(_barProgress, 0, 100);
    lv_obj_set_style_bg_color(_barProgress, UiTheme::kSurface(), LV_PART_MAIN);
    lv_obj_set_style_bg_color(_barProgress, UiTheme::kPrimary(), LV_PART_INDICATOR);

    lv_obj_t* stop = lv_btn_create(area);
    lv_obj_set_size(stop, 224, 40);
    lv_obj_align(stop, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_obj_set_style_bg_color(stop, UiTheme::kDanger(), 0);
    lv_obj_add_event_cb(stop, lvCbBtn, LV_EVENT_CLICKED, (void*)btnTag(2));
    lv_obj_t* sl = lv_label_create(stop);
    lv_label_set_text(sl, LV_SYMBOL_STOP " STOP AZAN");
    lv_obj_center(sl);
}

static const char* kPrayerLabels[] = {"Fajr", "Sunrise", "Dhuhr", "Asr", "Maghrib", "Isha"};
static const int kPrayerDisplayIdx[] = {0, 2, 3, 4, 5};

void UiScreens::buildPrayerTimes(lv_obj_t* area) {
    _scroll = UiComponents::createScrollContent(area, 0, lv_obj_get_height(area));
    lv_obj_t* title = lv_label_create(_scroll);
    lv_label_set_text(title, "Prayer Times");
    lv_obj_set_style_text_color(title, UiTheme::kText(), 0);

    for (int k = 0; k < 5; k++) {
        int idx = kPrayerDisplayIdx[k];
        lv_obj_t* card = UiComponents::createCard(_scroll, 216, 52);
        _prayerCards[k] = card;
        lv_obj_t* nm = lv_label_create(card);
        lv_label_set_text(nm, kPrayerLabels[idx]);
        lv_obj_set_style_text_color(nm, UiTheme::kText(), 0);
        lv_obj_t* tm = lv_label_create(card);
        lv_label_set_text(tm, "--:--");
        lv_obj_align(tm, LV_ALIGN_TOP_RIGHT, 0, 0);
        _prayerTimeLabels[k] = tm;
    }

    lv_obj_t* ref = lv_btn_create(_scroll);
    lv_obj_set_width(ref, 216);
    lv_obj_add_event_cb(ref, [](lv_event_t*) {
        if (!g_active) return;
        UiCommand c{};
        c.cmd = UiCmd::RequestPrayerTimes;
        g_active->sendCmd(c);
    }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* rl = lv_label_create(ref);
    lv_label_set_text(rl, LV_SYMBOL_REFRESH " Refresh");
    lv_obj_center(rl);
}

void UiScreens::buildAzanSettings(lv_obj_t* area) {
    _scroll = UiComponents::createScrollContent(area, 0, lv_obj_get_height(area));

    lv_obj_t* card1 = UiComponents::createCard(_scroll, 216, 70);
    lv_obj_t* pl = lv_label_create(card1);
    lv_label_set_text(pl, "Pre-Fajr alarm");
    _swPreFajr = lv_switch_create(card1);
    lv_obj_align(_swPreFajr, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_add_event_cb(_swPreFajr, [](lv_event_t* e) {
        if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED || !g_active) return;
        UiCommand c{};
        c.cmd = UiCmd::SetPreFajr;
        c.pf.enabled = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
        g_active->sendCmd(c);
    }, LV_EVENT_VALUE_CHANGED, nullptr);

    lv_obj_t* card2 = UiComponents::createCard(_scroll, 216, 80);
    lv_label_set_text(lv_label_create(card2), "Volume");
    _sliderVol = lv_slider_create(card2);
    lv_slider_set_range(_sliderVol, 0, 100);
    lv_obj_set_width(_sliderVol, 190);
    lv_obj_align(_sliderVol, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_event_cb(_sliderVol, [](lv_event_t* e) {
        if (lv_event_get_code(e) != LV_EVENT_RELEASED || !g_active) return;
        UiCommand c{};
        c.cmd = UiCmd::SetVolume;
        c.vol.volumePct = (uint8_t)lv_slider_get_value(lv_event_get_target(e));
        g_active->sendCmd(c);
    }, LV_EVENT_RELEASED, nullptr);

    lv_obj_t* card3 = UiComponents::createCard(_scroll, 216, 120);
    lv_label_set_text(lv_label_create(card3), "Default Azan (SD /azan)");
    _listFiles = lv_list_create(card3);
    lv_obj_set_size(_listFiles, 200, 88);
    lv_obj_align(_listFiles, LV_ALIGN_BOTTOM_MID, 0, 0);
}

void UiScreens::buildFileManager(lv_obj_t* area) {
    lv_obj_t* tabs = lv_obj_create(area);
    lv_obj_set_size(tabs, lv_pct(100), 36);
    lv_obj_align(tabs, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_opa(tabs, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(tabs, 0, 0);
    lv_obj_set_flex_flow(tabs, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(tabs, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* ba = lv_btn_create(tabs);
    lv_label_set_text(lv_label_create(ba), "/azan");
    lv_obj_center(lv_obj_get_child(ba, 0));
    lv_obj_add_event_cb(ba, lvCbBtn, LV_EVENT_CLICKED, (void*)btnTag(10));

    lv_obj_t* bq = lv_btn_create(tabs);
    lv_label_set_text(lv_label_create(bq), "/quran");
    lv_obj_center(lv_obj_get_child(bq, 0));
    lv_obj_add_event_cb(bq, lvCbBtn, LV_EVENT_CLICKED, (void*)btnTag(11));

    lv_obj_t* bp = lv_btn_create(tabs);
    lv_label_set_text(lv_label_create(bp), "Player");
    lv_obj_center(lv_obj_get_child(bp, 0));
    lv_obj_add_event_cb(bp, lvCbBtn, LV_EVENT_CLICKED, (void*)btnTag(12));

    _listFiles = lv_list_create(area);
    lv_obj_set_size(_listFiles, lv_pct(100), lv_obj_get_height(area) - 40);
    lv_obj_align(_listFiles, LV_ALIGN_BOTTOM_MID, 0, 0);
}

void UiScreens::buildQuranPlayer(lv_obj_t* area) {
    lv_obj_t* title = lv_label_create(area);
    lv_label_set_text(title, "Quran Player");
    lv_obj_set_style_text_color(title, UiTheme::kText(), 0);

    _listFiles = lv_list_create(area);
    lv_obj_set_size(_listFiles, lv_pct(100), lv_obj_get_height(area) - 70);
    lv_obj_align(_listFiles, LV_ALIGN_BOTTOM_MID, 0, -44);

    lv_obj_t* row = lv_obj_create(area);
    lv_obj_set_size(row, lv_pct(100), 40);
    lv_obj_align(row, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* stop = lv_btn_create(row);
    lv_obj_add_event_cb(stop, lvCbBtn, LV_EVENT_CLICKED, (void*)btnTag(2));
    lv_label_set_text(lv_label_create(stop), LV_SYMBOL_STOP);
    lv_obj_center(lv_obj_get_child(stop, 0));
}

void UiScreens::buildSystem(lv_obj_t* area) {
    _scroll = UiComponents::createScrollContent(area, 0, lv_obj_get_height(area));

    lv_obj_t* title = lv_label_create(_scroll);
    lv_label_set_text(title, "Bluetooth Transfer");
    lv_obj_set_style_text_color(title, UiTheme::kText(), 0);

    lv_obj_t* card = UiComponents::createCard(_scroll, 216, 160);
    _lblSystem = lv_label_create(card);
    lv_label_set_text(_lblSystem, "Transfer mode: OFF\nStatus: Offline\n(SPP not implemented)");
    lv_label_set_long_mode(_lblSystem, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(_lblSystem, 200);
    lv_obj_align(_lblSystem, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t* lblSw = lv_label_create(card);
    lv_label_set_text(lblSw, "Transfer Mode");
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
    lv_obj_align(progLbl, LV_ALIGN_TOP_LEFT, 0, 0);
    _barBtProgress = lv_bar_create(progCard);
    lv_obj_set_size(_barBtProgress, 200, 12);
    lv_obj_align(_barBtProgress, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_bar_set_range(_barBtProgress, 0, 100);
    lv_bar_set_value(_barBtProgress, 0, LV_ANIM_OFF);
}

void UiScreens::populateFileList(const UiEventPayload& ev) {
    if (!_listFiles) return;
    lv_obj_clean(_listFiles);
    _filePathCount = ev.fileCount > 16 ? 16 : ev.fileCount;

    const char* folder = ev.listFolder[0] ? ev.listFolder : _listFolder;

    for (uint8_t i = 0; i < _filePathCount; i++) {
        if (folder[0] == '/' && folder[1]) {
            snprintf(_filePaths[i], sizeof(_filePaths[i]), "%s/%s", folder, ev.files[i].name);
        } else {
            snprintf(_filePaths[i], sizeof(_filePaths[i]), "/%s", ev.files[i].name);
        }

        lv_obj_t* btn = lv_list_add_btn(_listFiles, LV_SYMBOL_AUDIO, ev.files[i].name);
        lv_obj_add_event_cb(btn, [](lv_event_t* e) {
            if (lv_event_get_code(e) != LV_EVENT_CLICKED || !g_active) return;
            int idx = (int)(intptr_t)lv_event_get_user_data(e);
            if (idx < 0 || idx >= g_active->_filePathCount) return;
            UiCommand c{};
            if (g_active->_screen == UiScreenId::AzanSettings) {
                c.cmd = UiCmd::SelectAzanFile;
                strncpy(c.azanPath.path, g_active->_filePaths[idx], sizeof(c.azanPath.path) - 1);
            } else {
                c.cmd = UiCmd::PlayFile;
                strncpy(c.play.path, g_active->_filePaths[idx], sizeof(c.play.path) - 1);
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
                                                [](lv_event_t* ev) {
                    if (!g_active) return;
                    UiCommand c{};
                    c.cmd = UiCmd::DeleteFile;
                    strncpy(c.del.path, g_active->_pendingDelete, sizeof(c.del.path) - 1);
                    g_active->sendCmd(c);
                    UiComponents::dismissDialog(lv_obj_get_parent(lv_obj_get_parent(
                        lv_event_get_target(ev))));
                }, nullptr);
            }, LV_EVENT_LONG_PRESSED, (void*)(intptr_t)i);
        }
    }
}

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
                snprintf(buf, sizeof(buf), "%04d-%02d-%02d", ev.year, ev.month, ev.mday);
                lv_label_set_text(_lblDate, buf);
            }
            break;

        case UiEvent::PrayerTimesUpdate:
            _lastPrayer = ev;
            if (_lblPrayerNow && _screen == UiScreenId::Home) {
                if (ev.currentPrayerIndex >= 0) {
                    snprintf(buf, sizeof(buf), "%s now", ev.currentPrayerName);
                } else {
                    snprintf(buf, sizeof(buf), "Between prayers");
                }
                lv_label_set_text(_lblPrayerNow, buf);
            }
            if (_lblNextPrayer && ev.nextPrayerIndex >= 0) {
                char cd[16];
                formatCountdown(cd, sizeof(cd), ev.secondsToNext);
                snprintf(buf, sizeof(buf), "%s in %s", ev.nextPrayerName, cd);
                lv_label_set_text(_lblNextPrayer, buf);
            }
            if (_barProgress && ev.secondsToNext > 0) {
                int pct = 100 - (ev.secondsToNext % 3600) / 36;
                if (pct < 5) pct = 5;
                if (pct > 100) pct = 100;
                lv_bar_set_value(_barProgress, pct, LV_ANIM_ON);
            }
            if (_screen == UiScreenId::PrayerTimes) {
                for (int k = 0; k < 5; k++) {
                    int idx = kPrayerDisplayIdx[k];
                    if (_prayerTimeLabels[k]) {
                        int m = ev.prayerMinutes[idx];
                        snprintf(buf, sizeof(buf), "%02d:%02d", m / 60, m % 60);
                        lv_label_set_text(_prayerTimeLabels[k], buf);
                    }
                    if (_prayerCards[k]) {
                        if (idx == ev.currentPrayerIndex || idx == ev.nextPrayerIndex) {
                            lv_obj_set_style_border_color(_prayerCards[k], UiTheme::kAccent(), 0);
                            lv_obj_set_style_border_width(_prayerCards[k], 2, 0);
                        } else {
                            lv_obj_set_style_border_width(_prayerCards[k], 1, 0);
                            lv_obj_set_style_border_color(_prayerCards[k], lv_color_hex(0x30363d), 0);
                        }
                    }
                }
            }
            break;

        case UiEvent::BluetoothStatus:
            if (_swTransfer) {
                if (ev.btEnabled) lv_obj_add_state(_swTransfer, LV_STATE_CHECKED);
                else lv_obj_clear_state(_swTransfer, LV_STATE_CHECKED);
            }
            if (_barBtProgress) {
                lv_bar_set_value(_barBtProgress, ev.btProgressPct, LV_ANIM_OFF);
            }
            if (_lblSystem && _screen == UiScreenId::Bluetooth) {
                snprintf(buf, sizeof(buf),
                         "Transfer: %s\nConnected: %s\n%s",
                         ev.btEnabled ? "ON" : "OFF",
                         ev.btConnected ? "yes" : "no",
                         ev.btStatusMsg[0] ? ev.btStatusMsg : "Ready");
                lv_label_set_text(_lblSystem, buf);
            }
            break;

        case UiEvent::SystemStatus:
            if (_sliderVol) {
                lv_slider_set_value(_sliderVol, ev.volumePct, LV_ANIM_OFF);
            }
            if (_swPreFajr) {
                if (ev.preFajr) lv_obj_add_state(_swPreFajr, LV_STATE_CHECKED);
                else lv_obj_clear_state(_swPreFajr, LV_STATE_CHECKED);
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
                else lv_obj_clear_state(_swPreFajr, LV_STATE_CHECKED);
            }
            break;

        case UiEvent::FileListStreamStart:
            _streamCount = 0;
            if (_listFiles) {
                lv_obj_clean(_listFiles);
            }
            break;

        case UiEvent::FileListStreamEntry:
            if (_streamCount < 16 && ev.fileCount > 0) {
                _streamFiles[_streamCount++] = ev.files[0];
            }
            break;

        case UiEvent::FileListStreamEnd:
            break;

        case UiEvent::FileListReady:
            populateFileList(ev);
            break;

        case UiEvent::StorageBusy:
            break;

        case UiEvent::AudioState:
            break;

        case UiEvent::FileOpResult:
            if (_screen == UiScreenId::FileManager) {
                UiCommand c{};
                c.cmd = UiCmd::ListFolder;
                strncpy(c.list.path, _listFolder, sizeof(c.list.path) - 1);
                sendCmd(c);
            }
            break;

        default:
            break;
    }
}

void UiScreens::tickRefresh() {
    if (!AzanSafeMode::allowLvglTickRefresh()) {
        return;
    }
    uint32_t now = millis();
    if (now - _lastRefreshMs < 1000) return;
    _lastRefreshMs = now;
    if (_screen != UiScreenId::Home) return;
    UiCommand c{};
    c.cmd = UiCmd::RequestClock;
    sendCmd(c);
    c.cmd = UiCmd::RequestPrayerTimes;
    sendCmd(c);
}

#endif
