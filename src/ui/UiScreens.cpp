#include "ui/UiScreens.h"
#include "UIManager.h"

#if defined(MINI_AZAN_UI_ENABLE) && MINI_AZAN_UI_ENABLE

static UiScreens* g_active = nullptr;

static void bridgePost(UiBridge* b, UiCommand cmd, bool urgent = false) {
    if (!b) return;
    if (urgent) b->postUrgent(cmd, 0);
    else b->postCommand(cmd, 0);
}

void UiScreens::lvCbBtn(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    uintptr_t tag = (uintptr_t)lv_event_get_user_data(e);
    if (!g_active || !g_active->_bridge) return;
    UiBridge* b = g_active->_bridge;

    uint8_t op = (uint8_t)(tag >> 24);
    uint8_t arg = (uint8_t)((tag >> 16) & 0xFF);

    switch (op) {
        case 1: { // nav
            UiCommand c{};
            c.cmd = UiCmd::Navigate;
            c.nav.screen = (UiScreenId)arg;
            c.nav.pushStack = true;
            bridgePost(b, c);
            g_active->show((UiScreenId)arg);
            break;
        }
        case 2: // stop
            UIManager::requestStopAzan(*b);
            break;
        case 3: { // toggle wifi
            UiCommand c{};
            c.cmd = UiCmd::ToggleWifi;
            bridgePost(b, c);
            break;
        }
        case 4: { // back
            if (g_active->_nav && g_active->_nav->pop()) {
                g_active->show(g_active->_nav->current());
            }
            break;
        }
        case 5: { // list audio
            UiCommand c{};
            c.cmd = UiCmd::ListAudioFiles;
            bridgePost(b, c);
            break;
        }
        case 6: { // list all files
            UiCommand c{};
            c.cmd = UiCmd::RefreshFileList;
            bridgePost(b, c);
            break;
        }
        case 7: { // refresh wifi status
            UiCommand c{};
            c.cmd = UiCmd::RequestWifiStatus;
            bridgePost(b, c);
            break;
        }
        default:
            break;
    }
}

static uintptr_t btnTag(uint8_t op, uint8_t arg = 0) {
    return ((uintptr_t)op << 24) | ((uintptr_t)arg << 16);
}

static lv_obj_t* makeBtn(lv_obj_t* parent, const char* txt, uintptr_t tag) {
    lv_obj_t* b = lv_btn_create(parent);
    lv_obj_set_width(b, lv_pct(100));
    lv_obj_add_event_cb(b, UiScreens::lvCbBtn, LV_EVENT_CLICKED, (void*)tag);
    lv_obj_t* l = lv_label_create(b);
    lv_label_set_text(l, txt);
    lv_obj_center(l);
    return b;
}

void UiScreens::begin(UiBridge& bridge, UiScreenMachine& nav) {
    _bridge = &bridge;
    _nav = &nav;
    g_active = this;

    _root = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(_root, lv_color_hex(0x1a252f), 0);

    _header = lv_obj_create(_root);
    lv_obj_set_size(_header, lv_pct(100), 36);
    lv_obj_align(_header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(_header, lv_color_hex(0x2c3e50), 0);

    _content = lv_obj_create(_root);
    lv_obj_set_size(_content, lv_pct(100), lv_pct(100));
    lv_obj_align(_content, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_y(_content, 36);
    lv_obj_set_height(_content, lv_obj_get_height(_root) - 36);
    lv_obj_set_style_pad_all(_content, 6, 0);

    lv_scr_load(_root);
    show(UiScreenId::Home);

    UiCommand c{};
    c.cmd = UiCmd::RequestClock;
    bridgePost(_bridge, c);
    c.cmd = UiCmd::RequestPrayerTimes;
    bridgePost(_bridge, c);
    c.cmd = UiCmd::RequestWifiStatus;
    bridgePost(_bridge, c);
}

void UiScreens::postNav(UiScreenId id, bool push) {
    if (push && _nav) _nav->push(id);
    show(id);
}

void UiScreens::sendCmd(UiCommand cmd, bool urgent) {
    bridgePost(_bridge, cmd, urgent);
}

void UiScreens::clearContent() {
    lv_obj_clean(_content);
    _lblClock = nullptr;
    _lblPrayer = nullptr;
    _lblStatus = nullptr;
    _sliderVol = nullptr;
    _swPreFajr = nullptr;
    _listFiles = nullptr;
    _swWifi = nullptr;
}

void UiScreens::show(UiScreenId id) {
    if (_nav) _nav->replace(id);
    lv_obj_clean(_header);
    clearContent();
    switch (id) {
        case UiScreenId::Home: buildHome(); break;
        case UiScreenId::Wifi: buildWifi(); break;
        case UiScreenId::Volume: buildVolume(); break;
        case UiScreenId::AzanSelect: buildAzan(); break;
        case UiScreenId::PreFajr: buildPreFajr(); break;
        case UiScreenId::Files: buildFiles(); break;
        default: buildHome(); break;
    }
}

void UiScreens::buildHome() {
    lv_obj_t* t = lv_label_create(_header);
    lv_label_set_text(t, "Mini Azan");
    lv_obj_center(t);

    _lblClock = lv_label_create(_content);
    lv_label_set_text(_lblClock, "--:--:--");
    lv_obj_set_style_text_font(_lblClock, &lv_font_montserrat_20, 0);

    _lblPrayer = lv_label_create(_content);
    lv_label_set_text(_lblPrayer, "Prayer: loading...");
    lv_obj_align(_lblPrayer, LV_ALIGN_TOP_LEFT, 0, 36);

    _lblStatus = lv_label_create(_content);
    lv_label_set_text(_lblStatus, "WiFi: ?  Audio: ?");
    lv_obj_align(_lblStatus, LV_ALIGN_TOP_LEFT, 0, 60);

    lv_obj_t* stop = lv_btn_create(_content);
    lv_obj_set_size(stop, lv_pct(100), 44);
    lv_obj_align(stop, LV_ALIGN_TOP_MID, 0, 88);
    lv_obj_set_style_bg_color(stop, lv_color_hex(0xc0392b), 0);
    lv_obj_add_event_cb(stop, lvCbBtn, LV_EVENT_CLICKED, (void*)btnTag(2));
    lv_obj_t* sl = lv_label_create(stop);
    lv_label_set_text(sl, "STOP AZAN");
    lv_obj_center(sl);

    makeBtn(_content, "WiFi", btnTag(1, (uint8_t)UiScreenId::Wifi));
    lv_obj_align(lv_obj_get_child(_content, -1), LV_ALIGN_TOP_MID, 0, 140);
    makeBtn(_content, "Volume", btnTag(1, (uint8_t)UiScreenId::Volume));
    makeBtn(_content, "Azan File", btnTag(1, (uint8_t)UiScreenId::AzanSelect));
    makeBtn(_content, "Pre-Fajr", btnTag(1, (uint8_t)UiScreenId::PreFajr));
    makeBtn(_content, "Files", btnTag(1, (uint8_t)UiScreenId::Files));
}

void UiScreens::buildWifi() {
    lv_obj_t* t = lv_label_create(_header);
    lv_label_set_text(t, "WiFi");
    lv_obj_center(t);
    makeBtn(_header, "<", btnTag(4));
    lv_obj_set_size(lv_obj_get_child(_header, 1), 40, 30);
    lv_obj_align(lv_obj_get_child(_header, 1), LV_ALIGN_LEFT_MID, 4, 0);

    _lblStatus = lv_label_create(_content);
    lv_label_set_text(_lblStatus, "Status: ...");

    _swWifi = lv_switch_create(_content);
    lv_obj_align(_swWifi, LV_ALIGN_TOP_LEFT, 0, 40);
    lv_obj_add_event_cb(_swWifi, [](lv_event_t* e) {
        if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
        UiCommand c{};
        c.cmd = UiCmd::ToggleWifi;
        if (g_active && g_active->_bridge) bridgePost(g_active->_bridge, c);
    }, LV_EVENT_VALUE_CHANGED, nullptr);

    makeBtn(_content, "Refresh", btnTag(7));
}

void UiScreens::buildVolume() {
    lv_obj_t* t = lv_label_create(_header);
    lv_label_set_text(t, "Volume");
    lv_obj_center(t);
    makeBtn(_header, "<", btnTag(4));
    lv_obj_set_size(lv_obj_get_child(_header, 1), 40, 30);
    lv_obj_align(lv_obj_get_child(_header, 1), LV_ALIGN_LEFT_MID, 4, 0);

    _sliderVol = lv_slider_create(_content);
    lv_slider_set_range(_sliderVol, 0, 21);
    lv_obj_set_width(_sliderVol, lv_pct(100));
    lv_obj_add_event_cb(_sliderVol, [](lv_event_t* e) {
        if (lv_event_get_code(e) != LV_EVENT_RELEASED) return;
        lv_obj_t* s = lv_event_get_target(e);
        UiCommand c{};
        c.cmd = UiCmd::SetVolume;
        c.vol.volume = (uint8_t)lv_slider_get_value(s);
        if (g_active && g_active->_bridge) bridgePost(g_active->_bridge, c);
    }, LV_EVENT_RELEASED, nullptr);
}

void UiScreens::buildAzan() {
    lv_obj_t* t = lv_label_create(_header);
    lv_label_set_text(t, "Azan");
    lv_obj_center(t);
    makeBtn(_header, "<", btnTag(4));
    lv_obj_set_size(lv_obj_get_child(_header, 1), 40, 30);
    lv_obj_align(lv_obj_get_child(_header, 1), LV_ALIGN_LEFT_MID, 4, 0);

    _listFiles = lv_list_create(_content);
    lv_obj_set_size(_listFiles, lv_pct(100), lv_pct(100));
    UiCommand c{};
    c.cmd = UiCmd::ListAudioFiles;
    bridgePost(_bridge, c);
}

void UiScreens::buildPreFajr() {
    lv_obj_t* t = lv_label_create(_header);
    lv_label_set_text(t, "Pre-Fajr");
    lv_obj_center(t);
    makeBtn(_header, "<", btnTag(4));
    lv_obj_set_size(lv_obj_get_child(_header, 1), 40, 30);
    lv_obj_align(lv_obj_get_child(_header, 1), LV_ALIGN_LEFT_MID, 4, 0);

    _swPreFajr = lv_switch_create(_content);
    lv_obj_align(_swPreFajr, LV_ALIGN_TOP_LEFT, 0, 8);
    lv_obj_add_event_cb(_swPreFajr, [](lv_event_t* e) {
        if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
        UiCommand c{};
        c.cmd = UiCmd::SetPreFajr;
        c.pf.enabled = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
        if (g_active && g_active->_bridge) bridgePost(g_active->_bridge, c);
    }, LV_EVENT_VALUE_CHANGED, nullptr);
}

void UiScreens::buildFiles() {
    lv_obj_t* t = lv_label_create(_header);
    lv_label_set_text(t, "Files");
    lv_obj_center(t);
    makeBtn(_header, "<", btnTag(4));
    lv_obj_set_size(lv_obj_get_child(_header, 1), 40, 30);
    lv_obj_align(lv_obj_get_child(_header, 1), LV_ALIGN_LEFT_MID, 4, 0);

    _listFiles = lv_list_create(_content);
    lv_obj_set_size(_listFiles, lv_pct(100), lv_pct(100));
    UiCommand c{};
    c.cmd = UiCmd::RefreshFileList;
    bridgePost(_bridge, c);
}

void UiScreens::onEvent(const UiEventPayload& ev) {
    char buf[96];
    switch (ev.type) {
        case UiEvent::ClockUpdate:
            if (_lblClock) {
                snprintf(buf, sizeof(buf), "%02d:%02d:%02d  [%s]", ev.hour, ev.minute, ev.second, ev.clockSource);
                lv_label_set_text(_lblClock, buf);
            }
            break;
        case UiEvent::PrayerTimesUpdate:
            if (_lblPrayer) {
                snprintf(buf, sizeof(buf), "F:%u D:%u A:%u | next %d min",
                         ev.prayerMinutes[0], ev.prayerMinutes[2], ev.prayerMinutes[3],
                         ev.nextPrayerMinutes);
                lv_label_set_text(_lblPrayer, buf);
            }
            break;
        case UiEvent::WifiStatus:
            if (_lblStatus && _nav && _nav->current() == UiScreenId::Home) {
                snprintf(buf, sizeof(buf), "WiFi:%s RSSI:%d", ev.wifiOn ? "ON" : "OFF", ev.wifiRssi);
                lv_label_set_text(_lblStatus, buf);
            }
            if (_swWifi) {
                if (ev.wifiOn) lv_obj_add_state(_swWifi, LV_STATE_CHECKED);
                else lv_obj_clear_state(_swWifi, LV_STATE_CHECKED);
            }
            if (_nav->current() == UiScreenId::Wifi && _lblStatus) {
                snprintf(buf, sizeof(buf), "WiFi:%s\n%s RSSI:%d", ev.wifiOn ? "ON" : "OFF", ev.ip, ev.wifiRssi);
                lv_label_set_text(_lblStatus, buf);
            }
            break;
        case UiEvent::VolumeState:
            if (_sliderVol) lv_slider_set_value(_sliderVol, ev.volume, LV_ANIM_OFF);
            break;
        case UiEvent::PreFajrState:
            if (_swPreFajr) {
                if (ev.preFajr) lv_obj_add_state(_swPreFajr, LV_STATE_CHECKED);
                else lv_obj_clear_state(_swPreFajr, LV_STATE_CHECKED);
            }
            break;
        case UiEvent::FileListReady:
            if (_listFiles) {
                lv_obj_clean(_listFiles);
                for (uint8_t i = 0; i < ev.fileCount; i++) {
                    lv_obj_t* b = lv_list_add_btn(_listFiles, LV_SYMBOL_FILE, ev.files[i].name);
                    lv_obj_set_user_data(b, (void*)(uintptr_t)i);
                    if (_nav->current() == UiScreenId::AzanSelect) {
                        lv_obj_add_event_cb(b, [](lv_event_t* e) {
                            if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
                            lv_obj_t* btn = lv_event_get_target(e);
                            const char* name = lv_list_get_btn_text(lv_obj_get_parent(btn), btn);
                            if (!g_active || !g_active->_bridge || !name) return;
                            UiCommand c{};
                            c.cmd = UiCmd::SelectAzanFile;
                            strncpy(c.azanPath.path, name, sizeof(c.azanPath.path) - 1);
                            bridgePost(g_active->_bridge, c);
                        }, LV_EVENT_CLICKED, nullptr);
                    } else if (_nav->current() == UiScreenId::Files) {
                        lv_obj_add_event_cb(b, [](lv_event_t* e) {
                            if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
                            lv_obj_t* btn = lv_event_get_target(e);
                            const char* name = lv_list_get_btn_text(lv_obj_get_parent(btn), btn);
                            if (!g_active || !g_active->_bridge || !name) return;
                            UiCommand c{};
                            c.cmd = UiCmd::DeleteFile;
                            strncpy(c.del.name, name, sizeof(c.del.name) - 1);
                            bridgePost(g_active->_bridge, c);
                            UiCommand r{};
                            r.cmd = UiCmd::RefreshFileList;
                            bridgePost(g_active->_bridge, r);
                        }, LV_EVENT_CLICKED, nullptr);
                    }
                }
            }
            break;
        case UiEvent::StorageBusy:
            if (_lblStatus) lv_label_set_text(_lblStatus, "SD busy (audio)");
            break;
        case UiEvent::AudioState:
            if (_lblStatus && _nav->current() == UiScreenId::Home) {
                snprintf(buf, sizeof(buf), "Audio: %s", ev.audioPlaying ? "PLAYING" : "idle");
                lv_label_set_text(_lblStatus, buf);
            }
            break;
        default:
            break;
    }
}

void UiScreens::tickRefresh() {
    uint32_t now = millis();
    if (now - _lastRefreshMs < 1000) return;
    _lastRefreshMs = now;
    if (_nav->current() != UiScreenId::Home) return;
    UiCommand c{};
    c.cmd = UiCmd::RequestClock;
    bridgePost(_bridge, c);
    c.cmd = UiCmd::RequestPrayerTimes;
    bridgePost(_bridge, c);
}

#endif
