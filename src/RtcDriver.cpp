#include "RtcDriver.h"
#include <cstring>
#include <sys/time.h>

bool RtcDriver::begin(TwoWire& wire) {
    _present = _rtc.begin(&wire);
    _lostPower = false;
    if (_present) {
        _lostPower = _rtc.lostPower();
    }
    return _present;
}

bool RtcDriver::readTime(struct tm& out) {
    if (!_present) return false;

    DateTime now = _rtc.now();
    memset(&out, 0, sizeof(out));
    out.tm_year = now.year() - 1900;
    out.tm_mon = now.month() - 1;
    out.tm_mday = now.day();
    out.tm_hour = now.hour();
    out.tm_min = now.minute();
    out.tm_sec = now.second();
    out.tm_wday = now.dayOfTheWeek();
    out.tm_isdst = -1;
    mktime(&out);
    return true;
}

bool RtcDriver::writeTime(const struct tm& in) {
    if (!_present) return false;

    DateTime dt(
        in.tm_year + 1900,
        in.tm_mon + 1,
        in.tm_mday,
        in.tm_hour,
        in.tm_min,
        in.tm_sec);
    _rtc.adjust(dt);
    _lostPower = false;
    return true;
}

bool RtcDriver::isTimeValid(const struct tm& t) const {
    if (t.tm_year < (MIN_VALID_YEAR - 1900)) return false;
    if (t.tm_mon < 0 || t.tm_mon > 11) return false;
    if (t.tm_mday < 1 || t.tm_mday > 31) return false;
    if (t.tm_hour < 0 || t.tm_hour > 23) return false;
    if (t.tm_min < 0 || t.tm_min > 59) return false;
    if (t.tm_sec < 0 || t.tm_sec > 59) return false;
    return true;
}

float RtcDriver::readTemperatureC() {
    if (!_present) return NAN;
    return _rtc.getTemperature();
}
