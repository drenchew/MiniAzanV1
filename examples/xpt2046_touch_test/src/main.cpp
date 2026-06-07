/**
 * XPT2046 touch lab — calibration, rotation, filtering (no LVGL).
 * Pins match MiniAzan HSPI layout. T_IRQ = GPIO34 + 10k pull-up to 3.3V.
 */
#include <Arduino.h>
#include <SPI.h>
#include "SpiArchitecture.h"
#include "ui/Xpt2046Touch.h"

static Xpt2046Touch touch;
static const char* kCornerName[] = {"TOP-LEFT", "TOP-RIGHT", "BOTTOM-RIGHT", "BOTTOM-LEFT"};

static int calStep = -1;
static bool calWaitRelease = false;
static uint16_t calRawX[4];
static uint16_t calRawY[4];
static uint32_t lastStreamMs = 0;
static bool wasDown = false;

static void printHelp() {
    Serial.println();
    Serial.println("Commands:");
    Serial.println("  help          - this message");
    Serial.println("  r0 r90 r180 r270 - rotation mode");
    Serial.println("  cal           - 4-corner calibration");
    Serial.println("  status        - cal + rotation");
    Serial.println("  stream        - toggle live raw/filt while touching");
    Serial.println();
}

static void printStatus() {
    const auto& c = touch.calibration();
    Serial.printf("rotation=%u cal_valid=%d\n", (unsigned)touch.rotation(), c.valid ? 1 : 0);
    Serial.printf("  rawX [%ld .. %ld]  rawY [%ld .. %ld]\n",
                  (long)c.rawXMin, (long)c.rawXMax, (long)c.rawYMin, (long)c.rawYMax);
    Serial.println("screen output clamped to 0..239, 0..319");
}

static void handleCalCommand() {
    calStep = 0;
    calWaitRelease = false;
    Serial.println("=== 4-corner calibration ===");
    Serial.printf("Touch %s corner, hold ~1s, then lift finger\n", kCornerName[0]);
}

static void handleSerialCommand(const String& line) {
    if (line == "help" || line == "?") {
        printHelp();
    } else if (line == "r0") {
        touch.setRotation(Xpt2046Touch::Rotation::R0);
        Serial.println("rotation=0");
    } else if (line == "r90") {
        touch.setRotation(Xpt2046Touch::Rotation::R90);
        Serial.println("rotation=90");
    } else if (line == "r180") {
        touch.setRotation(Xpt2046Touch::Rotation::R180);
        Serial.println("rotation=180");
    } else if (line == "r270") {
        touch.setRotation(Xpt2046Touch::Rotation::R270);
        Serial.println("rotation=270");
    } else if (line == "cal") {
        handleCalCommand();
    } else if (line == "status") {
        printStatus();
    } else if (line == "stream") {
        lastStreamMs = 0;
        Serial.println("streaming ON while finger down (100ms)");
    }
}

void setup() {
    Serial.begin(115200);
    delay(400);
    Serial.println();
    Serial.println("=== XPT2046 touch lab (240x320) ===");

    SpiArch::initUiBus();

    Xpt2046Touch::Config cfg;
    cfg.irqPin = SpiArch::UI_TOUCH_IRQ;
    cfg.csPin = SpiArch::UI_TOUCH_CS;
    cfg.tftCsPin = SpiArch::UI_TFT_CS;
    cfg.screenW = 240;
    cfg.screenH = 320;
    cfg.pressDebounce = 3;
    cfg.releaseDebounce = 3;
    cfg.spiAvgSamples = 4;
    cfg.deadzonePx = 5;
    cfg.emaAlpha = 80;

    touch.begin(cfg, SpiArch::uiSpi());

    Xpt2046Touch::Calibration cal;
    cal.rawXMin = 350;
    cal.rawXMax = 3650;
    cal.rawYMin = 400;
    cal.rawYMax = 3600;
    cal.valid = true;
    touch.setCalibration(cal);
    touch.setRotation(Xpt2046Touch::Rotation::R0);

    printHelp();
    printStatus();
}

void loop() {
    if (Serial.available()) {
        String line = Serial.readStringUntil('\n');
        line.trim();
        if (line.length()) handleSerialCommand(line);
    }

    if (calStep >= 0 && calStep < 4) {
        if (calWaitRelease) {
            if (!touch.irqLow()) {
                calWaitRelease = false;
                if (calStep < 4) {
                    Serial.printf("Touch %s corner...\n", kCornerName[calStep]);
                }
            }
            delay(10);
            return;
        }

        uint16_t rx = 0, ry = 0;
        if (touch.captureCalibrationCorner(calStep, rx, ry)) {
            calRawX[calStep] = rx;
            calRawY[calStep] = ry;
            Serial.printf("corner %s captured raw_x=%u raw_y=%u — lift finger\n",
                          kCornerName[calStep], rx, ry);
            calStep++;
            calWaitRelease = true;
            if (calStep >= 4) {
                touch.commitCalibrationFromCorners(calRawX, calRawY);
                calStep = -1;
                calWaitRelease = false;
                Serial.println("calibration done");
                printStatus();
            }
        }
        delay(20);
        return;
    }

    Xpt2046Touch::TouchPoint pt{};
    const bool down = touch.getPointEx(pt);

    if (down && !wasDown) {
        Serial.printf(">>> PRESS raw=(%d,%d) map=(%d,%d) out=(%d,%d)\n",
                      pt.rawX, pt.rawY, pt.mappedX, pt.mappedY, pt.x, pt.y);
    } else if (!down && wasDown) {
        Serial.println(">>> RELEASE");
    }

    if (down && millis() - lastStreamMs >= 100) {
        lastStreamMs = millis();
        Serial.printf("raw=(%4d,%4d) map=(%3d,%3d) filt/out=(%3d,%3d) z_ok\n",
                      pt.rawX, pt.rawY, pt.mappedX, pt.mappedY, pt.x, pt.y);
    }

    wasDown = down;
    delay(5);
}
