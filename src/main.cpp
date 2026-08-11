#include <driver/i2s.h>
#include <math.h>
#include <Arduino.h>

/**
 * Quick AUX / internal-DAC tone test (ESP32 classic only).
 * Wiring: GPIO25 → 100µF cap (+) → AUX tip, GND → AUX sleeve.
 */
#define SAMPLE_RATE 44100
#define TONE_FREQ   1000

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("AUX DAC test on GPIO25...");

    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_DAC_BUILT_IN),
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_MSB,
        .intr_alloc_flags = 0,
        .dma_buf_count = 8,
        .dma_buf_len = 64,
        .use_apll = false,
        .tx_desc_auto_clear = true,
        .fixed_mclk = 0
    };

    i2s_driver_install(I2S_NUM_0, &i2s_config, 0, nullptr);
    i2s_set_dac_mode(I2S_DAC_CHANNEL_RIGHT_EN);

    Serial.println("Playing 1 kHz tone on GPIO25 (DAC).");
}

void loop() {
    static int16_t samples[256];
    static uint32_t phase = 0;

    for (int i = 0; i < 256; i++) {
        const float s = sinf(2.0f * PI * TONE_FREQ * (float)(phase + i) / (float)SAMPLE_RATE);
        const int16_t sample = (int16_t)(s * 12000);
        samples[i] = sample;
    }
    phase += 256;

    size_t bytes_written = 0;
    i2s_write(I2S_NUM_0, samples, sizeof(samples), &bytes_written, portMAX_DELAY);
}
