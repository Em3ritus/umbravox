#include "sound.h"
#if defined(WS_C5_35) && defined(UMBRAVOX_SCAN_ENGINE)
#include <Arduino.h>
#include <Wire.h>
#include <atomic>
#include <driver/i2s_std.h>
#include "ws_c5_bus.h"
#include "elog.h"
#include "../ui/alert_tone.h"

// Pins and codec address from Waveshare's own board code for this board (ESP32-C5-Touch-LCD-3.5 factory BSP):
// I2S BCLK 23, LRCLK 10, DOUT 25, no MCLK (the codec takes its clock from BCLK), ES8311 on the shared I2C bus at
// 0x18. The amplifier enable is expander bit 5, which ws_c5_expander.cpp already holds high from boot.
namespace {
constexpr gpio_num_t PIN_BCLK = GPIO_NUM_23, PIN_LRCLK = GPIO_NUM_10, PIN_DOUT = GPIO_NUM_25;
constexpr uint8_t CODEC = 0x18;
constexpr int AMPS[4] = {0, 4500, 12000, 22000};  // sample peak of 32767, per Settings::sound
constexpr int CODEC_VOLUME = 70;                 // percent
std::atomic<bool> busy{false};

bool wr(uint8_t reg, uint8_t v) { Wire.beginTransmission(CODEC); Wire.write(reg); Wire.write(v); return Wire.endTransmission() == 0; }
bool rd(uint8_t reg, uint8_t& v) {
    Wire.beginTransmission(CODEC); Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((int)CODEC, 1) != 1) return false;
    v = (uint8_t)Wire.read(); return true;
}
bool rmw(uint8_t reg, uint8_t keep, uint8_t set) { uint8_t v; return rd(reg, v) && wr(reg, (uint8_t)((v & keep) | set)); }

// Register sequence of Espressif's es8311 driver (esp-bsp components/es8311), playback only: 16 kHz, 32-bit slots so
// BCLK = 1.024 MHz is the codec's internal clock (table row 1024000 / 16000), I2S format, slave.
bool codecOpen() {
    wsbus::Guard g;
    bool ok = wr(0x00, 0x1F); delay(20);
    ok = ok && wr(0x00, 0x00) && wr(0x00, 0x80);
    ok = ok && wr(0x01, 0xBF);                   // all clocks on, MCLK from the BCLK pin
    ok = ok && rmw(0x06, 0xE0, 0x03) && wr(0x03, 0x10) && wr(0x04, 0x10) && wr(0x05, 0x00);
    ok = ok && rmw(0x02, 0x07, 0x10) && rmw(0x07, 0xC0, 0x00) && wr(0x08, 0xFF);
    ok = ok && rmw(0x00, 0xBF, 0x00);            // serial port: slave
    ok = ok && wr(0x09, 0x10) && wr(0x0A, 0x10); // 32-bit words
    ok = ok && wr(0x0D, 0x01) && wr(0x0E, 0x02) && wr(0x12, 0x00) && wr(0x13, 0x10) && wr(0x1C, 0x6A) && wr(0x37, 0x08);
    ok = ok && wr(0x32, (uint8_t)(CODEC_VOLUME * 256 / 100 - 1));
    ok = ok && rmw(0x31, (uint8_t)~0x60, 0x00);  // unmute
    return ok;
}
void codecMute() { wsbus::Guard g; rmw(0x31, 0xFF, 0x60); }

// One task owns the codec. A cold start needs ~0.85 s before sound comes through (a 0.3 s lead-in lost the front of
// every beep; 1 s was clean), so after a beep the task keeps the clocks running on silence for LINGER_MS and plays the
// next alert at once. Then it mutes and gives the I2S channel (3 KB of internal RAM) back.
std::atomic<int> pending{-1};
constexpr int COLD_LEAD_MS = 850, LINGER_MS = 15000;

void silence(i2s_chan_handle_t tx, int32_t* frame, int ms) {
    memset(frame, 0, 256 * sizeof(int32_t));
    for (int left = ms * alerttone::kRate / 1000; left > 0; left -= 128) { size_t w = 0; i2s_channel_write(tx, frame, 256 * sizeof(int32_t), &w, 200); }
}

// Opens everything and plays until the linger runs out. Returns false if the hardware could not be started.
bool session() {
    i2s_chan_handle_t tx = nullptr;
    i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    cc.dma_desc_num = 3; cc.dma_frame_num = 128; cc.auto_clear = true;
    if (i2s_new_channel(&cc, &tx, nullptr) != ESP_OK) { elog::event(elog::WARN, "sound", "no I2S channel"); return false; }
    bool ok = false;
    i2s_std_config_t sc = {};
    sc.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(alerttone::kRate);
    sc.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO);
    sc.gpio_cfg.mclk = I2S_GPIO_UNUSED; sc.gpio_cfg.bclk = PIN_BCLK; sc.gpio_cfg.ws = PIN_LRCLK;
    sc.gpio_cfg.dout = PIN_DOUT; sc.gpio_cfg.din = I2S_GPIO_UNUSED;
    if (i2s_channel_init_std_mode(tx, &sc) != ESP_OK || i2s_channel_enable(tx) != ESP_OK) elog::event(elog::WARN, "sound", "I2S start failed");
    else if (!codecOpen()) elog::event(elog::WARN, "sound", "ES8311 did not answer at 0x18");
    else {
        ok = true;
        int16_t pcm[128]; int32_t frame[256];
        silence(tx, frame, COLD_LEAD_MS);
        uint32_t idleSince = millis();
        for (;;) {
            const int req = pending.exchange(-1);
            if (req >= 0) {
                const int level = req >> 2, amp = AMPS[req & 3];
                for (int at = 0;;) {
                    const int n = alerttone::render(level, at, pcm, 128, amp);
                    if (n <= 0) break;
                    for (int i = 0; i < n; i++) frame[2 * i] = frame[2 * i + 1] = (int32_t)pcm[i] << 16;
                    size_t w = 0;
                    if (i2s_channel_write(tx, frame, (size_t)n * 8, &w, 200) != ESP_OK) break;
                    at += n;
                }
                idleSince = millis();
            } else {
                if ((int32_t)(millis() - idleSince) > LINGER_MS) break;
                silence(tx, frame, 8);
            }
        }
        silence(tx, frame, 30);
        codecMute();
    }
    i2s_channel_disable(tx);
    i2s_del_channel(tx);
    return ok;
}

void task(void*) {
    for (;;) {
        if (!session()) pending.store(-1);
        busy.store(false);
        if (pending.load() < 0) break;                  // an alert that arrived while closing: start again
        bool f = false;
        if (!busy.compare_exchange_strong(f, true)) break;
    }
    vTaskDelete(nullptr);
}
} // namespace

namespace sound {
void alert(uint8_t conf, uint16_t volume) {
    if (volume == 0) return;
    pending.store((conf > 2 ? 2 : conf) * 4 + (volume > 3 ? 3 : volume));
    bool exp = false;
    if (!busy.compare_exchange_strong(exp, true)) return;       // the task is up: it takes the request itself
    if (xTaskCreate(task, "beep", 4096, nullptr, 3, nullptr) != pdPASS) {
        elog::event(elog::WARN, "sound", "no memory for the beep task");
        busy.store(false); pending.store(-1);
    }
}
}
#else
namespace sound { void alert(uint8_t, uint16_t) {} }
#endif
