#pragma once

#include <Arduino.h>
#include <RTClib.h>
#include <Wire.h>

// Thin abstraction over DS3231 / DS3231M (same I2C protocol).
class RtcDriver {
public:
    static constexpr int MIN_VALID_YEAR = 2023;

    bool begin(TwoWire& wire = Wire);
    bool isPresent() const { return _present; }
    bool hasLostPower() const { return _lostPower; }

    bool readTime(struct tm& out);
    bool writeTime(const struct tm& in);
    bool isTimeValid(const struct tm& t) const;

    float readTemperatureC();

private:
    RTC_DS3231 _rtc;
    bool _present = false;
    bool _lostPower = false;
};
