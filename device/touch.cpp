#include "touch.h"
#include <Arduino.h>
#include "board.h"
#include "canvas.h"

#if defined(NM_CYD_C5)
#include <SPI.h>

namespace touch {
namespace {

constexpr int8_t PIN_CS = board::PIN_TOUCH_CS;   // the panel is on 23

// 2MHz. The XPT2046 datasheet allows a little more, but the conversion needs
// settling time and there is nothing to gain here -- a whole poll is five
// 24-bit transfers.
constexpr uint32_t SPI_HZ = 2000000;

// Measured on this panel by tapping the four corners, not taken from the
// board's published constants.
//
// The published numbers were close on *range* but said nothing about
// assignment, and the assignment is where this goes wrong: the touch layer
// does not share the display's rotation. Raw X runs high at the top of the
// screen and low at the bottom, so it carries screen Y; raw Y runs high on
// the left and low on the right, so it carries screen X. Both are inverted.
//
//   corner        rawX   rawY
//   top-left      3545   3474
//   top-right     3340    409
//   bottom-right   496    320
//   bottom-left    500   3524
//   centre        2096   1910
constexpr uint16_t CAL_X_BOTTOM = 350,  CAL_X_TOP  = 3720;   // rawX -> screen Y
constexpr uint16_t CAL_Y_RIGHT  = 240,  CAL_Y_LEFT = 3640;   // rawY -> screen X

// Below this the panel is not being pressed, it is just noise.
constexpr uint16_t Z_THRESHOLD = 400;

// Control bytes: start bit, channel, 12-bit differential mode.
constexpr uint8_t CMD_X  = 0xD0;
constexpr uint8_t CMD_Y  = 0x90;
constexpr uint8_t CMD_Z1 = 0xB0;
constexpr uint8_t CMD_Z2 = 0xC0;

uint16_t xfer(uint8_t cmd) {
    SPI.transfer(cmd);
    const uint8_t hi = SPI.transfer(0x00);
    const uint8_t lo = SPI.transfer(0x00);
    return (uint16_t)(((hi << 8) | lo) >> 3);   // 12 bits, left-aligned
}

int cmpU16(const void* a, const void* b) {
    const uint16_t x = *(const uint16_t*)a, y = *(const uint16_t*)b;
    return x < y ? -1 : (x > y ? 1 : 0);
}

// Median of three. Resistive panels are noisy and a single sample will
// occasionally land anywhere; the median throws out the flyer without the lag
// an averaging filter would add.
uint16_t median3(uint8_t cmd) {
    uint16_t v[3];
    for (int i = 0; i < 3; i++) v[i] = xfer(cmd);
    qsort(v, 3, sizeof(uint16_t), cmpU16);
    return v[1];
}

// `hi` is the raw value at screen coordinate 0, so the mapping runs backwards.
int16_t mapInv(uint16_t raw, uint16_t lo, uint16_t hi, int16_t out) {
    if (hi <= lo) return 0;
    if (raw < lo) raw = lo;
    if (raw > hi) raw = hi;
    long v = (long)(hi - raw) * out / (hi - lo);
    if (v < 0) v = 0;
    if (v >= out) v = out - 1;
    return (int16_t)v;
}

} // namespace

void begin() {
    pinMode(PIN_CS, OUTPUT);
    digitalWrite(PIN_CS, HIGH);
    // The bus itself (SPI.begin on the panel's pins) is setup()'s now, not
    // ours: the SD card needs it too, and on a board whose touch is not on
    // SPI there would otherwise be nobody to bring it up.
    Serial.println("touch: XPT2046 on CS 1, polled (no IRQ line on this board)");
}

Point read() {
    Point p;

    SPI.beginTransaction(SPISettings(SPI_HZ, MSBFIRST, SPI_MODE0));
    digitalWrite(PIN_CS, LOW);
    const uint16_t z1 = xfer(CMD_Z1);
    const uint16_t z2 = xfer(CMD_Z2);
    const uint16_t rx = median3(CMD_X);
    const uint16_t ry = median3(CMD_Y);
    digitalWrite(PIN_CS, HIGH);
    SPI.endTransaction();

    // Standard XPT2046 pressure estimate: rises as the plates are pushed
    // together, so a larger number means a firmer press.
    const int z = (int)z1 + 4095 - (int)z2;
    p.z    = (uint16_t)(z < 0 ? 0 : z);
    p.rawX = rx;
    p.rawY = ry;
    p.down = (p.z > Z_THRESHOLD) && rx > 100 && ry > 100;
    p.any  = p.down;

    if (p.down) {
        // Axes crossed: raw Y gives screen X, raw X gives screen Y.
        p.x = mapInv(ry, CAL_Y_RIGHT,  CAL_Y_LEFT, ui::W);
        p.y = mapInv(rx, CAL_X_BOTTOM, CAL_X_TOP,  ui::H);
    }
    return p;
}

} // namespace touch
#endif // NM_CYD_C5

#if defined(WS_C5_35)
#include <Wire.h>
#include "ws_c5_expander.h"

namespace touch {
namespace {

constexpr uint8_t ADDR      = 0x38;
constexpr uint8_t TD_STATUS = 0x02;   // point count, then 6 bytes of point 1
constexpr uint8_t ID_G_THGROUP = 0x80; // touch threshold; lower = more sensitive
constexpr uint8_t ID_G_CIPHER  = 0xA3; // chip id

// Native panel frame, before rotation: 320 across the short edge, 480 down
// the long one.
constexpr int NATIVE_W = 320;
constexpr int NATIVE_H = 480;

// A finger held still occasionally reads back as no points for one poll.
// Bridge a gap this short so a press does not read as a lift and re-press.
constexpr uint32_t DROPOUT_MS = 90;

bool present = false;

// Register read with a STOP between the address write and the read, not a
// repeated start. The bus is shared with the expander and the PMU, and on
// this board a repeated-start read is what can leave it wedged.
bool readRegs(uint8_t reg, uint8_t* buf, uint8_t len) {
    Wire.beginTransmission(ADDR);
    Wire.write(reg);
    if (Wire.endTransmission(true) != 0) return false;
    if (Wire.requestFrom((int)ADDR, (int)len) != len) return false;
    for (uint8_t i = 0; i < len; i++) buf[i] = (uint8_t)Wire.read();
    return true;
}

// One raw sample in the native frame. The C5 quirk: after the early
// Wire.begin(), reads stop completing (writes ACK, reads come back 0) until
// the controller is re-opened, so it is re-opened on every read. Verified on
// this board; do not remove.
bool sample(uint16_t& nx, uint16_t& ny) {
    for (int attempt = 0; attempt < 3; attempt++) {
        Wire.end();
        Wire.begin(board::PIN_SDA, board::PIN_SCL, 100000u);
        uint8_t d[5];
        if (!readRegs(TD_STATUS, d, sizeof d)) continue;
        const uint8_t n = d[0] & 0x0F;
        if (n == 0) return false;               // a clean "nobody there"
        if (n > 2) continue;                    // garbage; try again
        nx = (uint16_t)(((d[1] & 0x0F) << 8) | d[2]);
        ny = (uint16_t)(((d[3] & 0x0F) << 8) | d[4]);
        if (nx < NATIVE_W && ny < NATIVE_H) return true;
    }
    return false;
}

// Native portrait -> the landscape frame the display is drawn in. The panel
// runs at Arduino_GFX rotation board::ROTATION; this is the same turn.
void rotate(uint16_t nx, uint16_t ny, int& sx, int& sy) {
    if (board::ROTATION == 1) { sx = ny;                sy = NATIVE_W - 1 - nx; }
    else                      { sx = NATIVE_H - 1 - ny; sy = nx; }
}

} // namespace

void begin() {
    exio::begin();   // touch reset is on the expander; a no-op if already up
    uint8_t id = 0;
    present = readRegs(ID_G_CIPHER, &id, 1) && id == 0x64;   // FT6336U
    Serial.printf("touch: FT6336 on I2C 0x38, id 0x%02X %s\n", id,
                  present ? "(ok)" : "(not found -- no touch)");
    // 12 rather than the default 22: a bare panel misses light taps at 22.
    // Raise it toward 30-40 if a case or film brings phantom touches.
    Wire.beginTransmission(ADDR);
    Wire.write(ID_G_THGROUP);
    Wire.write(12);
    Wire.endTransmission();
}

Point read() {
    static uint16_t lastX = 0, lastY = 0;
    static uint32_t lastOk = 0;

    Point p;
    if (!present) return p;

    uint16_t nx, ny;
    const uint32_t now = millis();
    if (sample(nx, ny)) {
        lastX = nx; lastY = ny; lastOk = now;
    } else if (!(lastOk && now - lastOk < DROPOUT_MS)) {
        lastOk = 0;
        return p;
    }

    int sx, sy;
    rotate(lastX, lastY, sx, sy);
    p.rawX = lastX;
    p.rawY = lastY;
    p.z    = 0;            // capacitive: no pressure worth the name
    p.any  = true;

    // Off the 2x canvas is the status strip or the side margins: a contact,
    // but not one that lands on anything.
    const int cx = (sx - board::CANVAS_X) / 2;
    const int cy = (sy - board::CANVAS_Y) / 2;
    if (sx < board::CANVAS_X || sy < board::CANVAS_Y || cx >= ui::W || cy >= ui::H)
        return p;
    p.down = true;
    p.x = (int16_t)cx;
    p.y = (int16_t)cy;
    return p;
}

} // namespace touch
#endif // WS_C5_35
