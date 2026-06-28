#include "voice.h"

#include <Arduino.h>
#include <Wire.h>
#include <driver/i2s.h>
#include <string.h>

#include "ble.h"
#include "display_cfg.h"

#define VOICE_I2S_PORT I2S_NUM_1
#define VOICE_CAPTURE_SAMPLE_RATE 16000
#define VOICE_TX_SAMPLE_RATE 8000
#define VOICE_CHANNELS 1
#define VOICE_MAX_MS 5000UL
#define VOICE_READ_FRAMES 160
#define VOICE_READ_SAMPLES (VOICE_READ_FRAMES * 2)
#define VOICE_BLE_PAYLOAD_MAX 160
#define VOICE_SEND_INTERVAL_MS 12
#define VOICE_END_REPEAT 4
#define VOICE_MIC_GAIN 0x08

#define ES7210_RESET_REG00       0x00
#define ES7210_CLOCK_OFF_REG01   0x01
#define ES7210_MAINCLK_REG02     0x02
#define ES7210_LRCK_DIVH_REG04   0x04
#define ES7210_LRCK_DIVL_REG05   0x05
#define ES7210_POWER_DOWN_REG06  0x06
#define ES7210_OSR_REG07         0x07
#define ES7210_TIME_CONTROL0_REG09 0x09
#define ES7210_TIME_CONTROL1_REG0A 0x0A
#define ES7210_SDP_INTERFACE1_REG11 0x11
#define ES7210_SDP_INTERFACE2_REG12 0x12
#define ES7210_ANALOG_REG40      0x40
#define ES7210_MIC12_BIAS_REG41  0x41
#define ES7210_MIC34_BIAS_REG42  0x42
#define ES7210_MIC1_GAIN_REG43   0x43
#define ES7210_MIC2_GAIN_REG44   0x44
#define ES7210_MIC3_GAIN_REG45   0x45
#define ES7210_MIC4_GAIN_REG46   0x46
#define ES7210_MIC1_POWER_REG47  0x47
#define ES7210_MIC2_POWER_REG48  0x48
#define ES7210_MIC3_POWER_REG49  0x49
#define ES7210_MIC4_POWER_REG4A  0x4A
#define ES7210_MIC12_POWER_REG4B 0x4B
#define ES7210_MIC34_POWER_REG4C 0x4C

static bool voice_ready = false;
static bool voice_recording = false;
static uint32_t voice_started_ms = 0;
static uint16_t voice_seq = 0;
static uint32_t voice_samples_sent = 0;
static uint32_t voice_chunks_read = 0;
static uint32_t voice_zero_chunks = 0;
static uint32_t voice_nonzero_chunks = 0;
static uint16_t voice_peak_seen = 0;
static uint32_t voice_last_send_ms = 0;
static int16_t voice_buf[VOICE_READ_SAMPLES];
static int16_t voice_tx_buf[VOICE_READ_FRAMES / 2];

static bool es7210_write(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(ES7210_ADDR);
    Wire.write(reg);
    Wire.write(val);
    return Wire.endTransmission() == 0;
}

static int es7210_read(uint8_t reg) {
    Wire.beginTransmission(ES7210_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return -1;
    if (Wire.requestFrom(ES7210_ADDR, (uint8_t)1) != 1) return -1;
    return Wire.read();
}

static bool es7210_update(uint8_t reg, uint8_t mask, uint8_t val) {
    int cur = es7210_read(reg);
    if (cur < 0) return false;
    return es7210_write(reg, (uint8_t)((cur & ~mask) | (val & mask)));
}

static uint16_t voice_abs16(int16_t sample) {
    int32_t value = sample;
    if (value < 0) value = -value;
    return value > 32767 ? 32767 : (uint16_t)value;
}

static bool es7210_enable_all_mics(void) {
    bool ok = true;
    for (uint8_t reg = ES7210_MIC1_GAIN_REG43; reg <= ES7210_MIC4_GAIN_REG46; ++reg) {
        ok &= es7210_update(reg, 0x10, 0x00);
    }
    ok &= es7210_write(ES7210_MIC12_POWER_REG4B, 0xff);
    ok &= es7210_write(ES7210_MIC34_POWER_REG4C, 0xff);

    ok &= es7210_update(ES7210_CLOCK_OFF_REG01, 0x0b, 0x00);
    ok &= es7210_write(ES7210_MIC12_POWER_REG4B, 0x00);
    ok &= es7210_update(ES7210_MIC1_GAIN_REG43, 0x10, 0x10);
    ok &= es7210_update(ES7210_MIC2_GAIN_REG44, 0x10, 0x10);

    ok &= es7210_update(ES7210_CLOCK_OFF_REG01, 0x15, 0x00);
    ok &= es7210_write(ES7210_MIC34_POWER_REG4C, 0x00);
    ok &= es7210_update(ES7210_MIC3_GAIN_REG45, 0x10, 0x10);
    ok &= es7210_update(ES7210_MIC4_GAIN_REG46, 0x10, 0x10);

    ok &= es7210_update(ES7210_MIC1_GAIN_REG43, 0x0f, VOICE_MIC_GAIN);
    ok &= es7210_update(ES7210_MIC2_GAIN_REG44, 0x0f, VOICE_MIC_GAIN);
    ok &= es7210_update(ES7210_MIC3_GAIN_REG45, 0x0f, VOICE_MIC_GAIN);
    ok &= es7210_update(ES7210_MIC4_GAIN_REG46, 0x0f, VOICE_MIC_GAIN);
    return ok;
}

static bool es7210_begin_16k(void) {
    bool ok = true;
    ok &= es7210_write(ES7210_RESET_REG00, 0xff);
    delay(2);
    ok &= es7210_write(ES7210_RESET_REG00, 0x41);
    ok &= es7210_write(ES7210_CLOCK_OFF_REG01, 0x1f);
    ok &= es7210_write(ES7210_TIME_CONTROL0_REG09, 0x30);
    ok &= es7210_write(ES7210_TIME_CONTROL1_REG0A, 0x30);
    ok &= es7210_write(ES7210_ANALOG_REG40, 0xc3);
    ok &= es7210_write(ES7210_MIC12_BIAS_REG41, 0x70);
    ok &= es7210_write(ES7210_MIC34_BIAS_REG42, 0x70);

    // 16 kHz, 16-bit, normal I2S. Coefficients match Waveshare's ES7210 demo.
    ok &= es7210_write(ES7210_MAINCLK_REG02, 0xc1);
    ok &= es7210_write(ES7210_OSR_REG07, 0x20);
    ok &= es7210_write(ES7210_LRCK_DIVH_REG04, 0x01);
    ok &= es7210_write(ES7210_LRCK_DIVL_REG05, 0x00);
    ok &= es7210_write(ES7210_SDP_INTERFACE1_REG11, 0x60);
    ok &= es7210_write(ES7210_SDP_INTERFACE2_REG12, 0x00);

    ok &= es7210_enable_all_mics();
    ok &= es7210_write(ES7210_CLOCK_OFF_REG01, 0x00);
    ok &= es7210_write(ES7210_POWER_DOWN_REG06, 0x00);
    ok &= es7210_write(ES7210_MIC1_POWER_REG47, 0x00);
    ok &= es7210_write(ES7210_MIC2_POWER_REG48, 0x00);
    ok &= es7210_write(ES7210_MIC3_POWER_REG49, 0x00);
    ok &= es7210_write(ES7210_MIC4_POWER_REG4A, 0x00);
    ok &= es7210_enable_all_mics();
    return ok;
}

bool voice_init(void) {
    if (voice_ready) return true;

    if (es7210_read(ES7210_RESET_REG00) < 0) {
        Serial.println("Voice: ES7210 not found");
        return false;
    }
    if (!es7210_begin_16k()) {
        Serial.println("Voice: ES7210 init failed");
        return false;
    }

    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = VOICE_CAPTURE_SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 8,
        .dma_buf_len = 64,
        .use_apll = false,
        .tx_desc_auto_clear = true,
        .fixed_mclk = 0,
        .mclk_multiple = I2S_MCLK_MULTIPLE_256,
        .bits_per_chan = I2S_BITS_PER_CHAN_16BIT,
        .chan_mask = (i2s_channel_t)(I2S_TDM_ACTIVE_CH0 | I2S_TDM_ACTIVE_CH1),
    };

    i2s_pin_config_t pin_config = {};
    pin_config.bck_io_num = MIC_I2S_BCLK;
    pin_config.ws_io_num = MIC_I2S_LRCK;
    pin_config.data_in_num = MIC_I2S_DIN;
    pin_config.data_out_num = I2S_PIN_NO_CHANGE;
    pin_config.mck_io_num = MIC_I2S_MCLK;

    esp_err_t err = i2s_driver_install(VOICE_I2S_PORT, &i2s_config, 0, NULL);
    if (err != ESP_OK) {
        Serial.printf("Voice: i2s_driver_install failed: %d\n", (int)err);
        return false;
    }
    err = i2s_set_pin(VOICE_I2S_PORT, &pin_config);
    if (err != ESP_OK) {
        Serial.printf("Voice: i2s_set_pin failed: %d\n", (int)err);
        i2s_driver_uninstall(VOICE_I2S_PORT);
        return false;
    }
    err = i2s_set_clk(
        VOICE_I2S_PORT,
        VOICE_CAPTURE_SAMPLE_RATE,
        I2S_BITS_PER_SAMPLE_16BIT,
        I2S_CHANNEL_STEREO
    );
    if (err != ESP_OK) {
        Serial.printf("Voice: i2s_set_clk failed: %d\n", (int)err);
        i2s_driver_uninstall(VOICE_I2S_PORT);
        return false;
    }
    i2s_zero_dma_buffer(VOICE_I2S_PORT);
    voice_ready = true;
    Serial.println("Voice: ready");
    return true;
}

bool voice_start(void) {
    if (!voice_ready && !voice_init()) return false;
    if (!ble_send_voice_start(VOICE_TX_SAMPLE_RATE, VOICE_CHANNELS)) return false;
    voice_recording = true;
    voice_started_ms = millis();
    voice_last_send_ms = 0;
    voice_seq = 0;
    voice_samples_sent = 0;
    voice_chunks_read = 0;
    voice_zero_chunks = 0;
    voice_nonzero_chunks = 0;
    voice_peak_seen = 0;
    i2s_zero_dma_buffer(VOICE_I2S_PORT);
    i2s_start(VOICE_I2S_PORT);
    delay(20);
    Serial.println("Voice: recording");
    return true;
}

void voice_stop(void) {
    if (!voice_recording) return;
    voice_recording = false;
    for (int i = 0; i < VOICE_END_REPEAT; ++i) {
        ble_send_voice_end();
        delay(25);
    }
    Serial.printf(
        "Voice: stopped, samples=%lu chunks=%lu peak=%u nonzero=%lu zero=%lu\n",
        (unsigned long)voice_samples_sent,
        (unsigned long)voice_chunks_read,
        voice_peak_seen,
        (unsigned long)voice_nonzero_chunks,
        (unsigned long)voice_zero_chunks
    );
}

void voice_tick(void) {
    if (!voice_recording) return;
    if (millis() - voice_started_ms > VOICE_MAX_MS) {
        voice_stop();
        return;
    }
    uint32_t now = millis();
    if (voice_last_send_ms && now - voice_last_send_ms < VOICE_SEND_INTERVAL_MS) return;

    size_t bytes_read = 0;
    esp_err_t err = i2s_read(
        VOICE_I2S_PORT,
        voice_buf,
        sizeof(voice_buf),
        &bytes_read,
        pdMS_TO_TICKS(8)
    );
    if (err != ESP_OK || bytes_read == 0) return;

    size_t samples_read = bytes_read / sizeof(int16_t);
    size_t frames_read = samples_read / 2;
    if (frames_read == 0) return;

    uint16_t left_peak = 0;
    uint16_t right_peak = 0;
    for (size_t frame = 0; frame < frames_read; ++frame) {
        uint16_t left = voice_abs16(voice_buf[frame * 2]);
        uint16_t right = voice_abs16(voice_buf[frame * 2 + 1]);
        if (left > left_peak) left_peak = left;
        if (right > right_peak) right_peak = right;
    }

    const uint8_t channel_offset = right_peak > left_peak ? 1 : 0;
    const uint16_t chunk_peak = right_peak > left_peak ? right_peak : left_peak;
    ++voice_chunks_read;
    if (chunk_peak > voice_peak_seen) voice_peak_seen = chunk_peak;
    if (chunk_peak > 0) {
        ++voice_nonzero_chunks;
    } else {
        ++voice_zero_chunks;
    }
    if (voice_chunks_read <= 8 || (voice_chunks_read % 50) == 0) {
        Serial.printf(
            "Voice: chunk=%lu bytes=%u left=%u right=%u ch=%u\n",
            (unsigned long)voice_chunks_read,
            (unsigned)bytes_read,
            left_peak,
            right_peak,
            channel_offset
        );
    }

    size_t tx_samples = 0;
    for (size_t frame = 0; frame < frames_read && tx_samples < (VOICE_READ_FRAMES / 2); frame += 2) {
        voice_tx_buf[tx_samples++] = voice_buf[frame * 2 + channel_offset];
    }
    size_t tx_bytes = tx_samples * sizeof(int16_t);
    if (tx_bytes == 0) return;
    if (tx_bytes > VOICE_BLE_PAYLOAD_MAX) tx_bytes = VOICE_BLE_PAYLOAD_MAX;
    if (!ble_send_voice_chunk(voice_seq++, (const uint8_t*)voice_tx_buf, tx_bytes)) {
        voice_recording = false;
        return;
    }
    voice_last_send_ms = now;
    voice_samples_sent += tx_bytes / sizeof(int16_t);
}

bool voice_is_ready(void) {
    return voice_ready;
}

bool voice_is_recording(void) {
    return voice_recording;
}
