#if (defined(WS_C5_35) || defined(WS_C5_28))
#include "ws_c5_gps.h"
#include <Arduino.h>
#include <Wire.h>
#include "ws_c5_bus.h"
#include "i2c_budget.h"
#include "board.h"

// Waveshare LC76G_RPI_I2C (i2c_read.c / i2c_write.c), one read cycle:
//   0. 1-byte dummy write to 0x50 (wake / presence)        [Recovery on NACK]
//   1. write {reg 0xAA510008, len 4} (LE) to 0x50  -> "how much is queued?"
//   2. read 4 bytes from 0x54                      -> length (LE)
//   3. write {reg 0xAA512000, len N} (LE) to 0x50  -> "send N"
//   4. read N bytes from 0x54                      -> NMEA text
// The demo settles 10 ms before each step. The module clock-stretches, so
// 3 ms is enough and keeps the stall short; a cycle that comes back empty
// costs nothing, a long loop stall costs touch.
//
// Engine builds sample touch on a periodic task. The whole GPS protocol cycle
// retains bus ownership (never reopen between commands). Shorter payloads keep
// the same drain capacity; a remaining-time budget prevents per-step timeouts
// accumulating. Driver/controller restart and task scheduling are measured on
// hardware, not asserted to have a hard real-time bound by this source.

namespace gpsi2c {
namespace {

constexpr uint8_t  CR_ADDR   = 0x50;   // command/register writes
constexpr uint8_t  RD_ADDR   = 0x54;   // data reads
constexpr uint8_t  WR_ADDR   = 0x58;   // data writes (wake)
#if defined(UMBRAVOX_SCAN_ENGINE)
// Same 1 KiB/s drain capacity as 512 bytes / 500 ms, shorter atomic ownership.
constexpr uint32_t CHUNK = 128;
constexpr uint32_t PERIOD_MS = 125;
#else
constexpr uint32_t CHUNK = 512;
constexpr uint32_t PERIOD_MS = 500;
#endif
constexpr uint32_t STEP_MS   = 3;      // per-step settle (clock-stretch is the real settle)
constexpr uint16_t TIMEOUT_MS = 100;   // fail-fast: a healthy step answers in a few ms, so
                                       // 100ms still covers real clock-stretch but a cold/
                                       // non-answering step fails fast instead of blocking 300ms
constexpr uint32_t WAKE_AFTER = 4000 / PERIOD_MS;     // empty cycles in a row before a wake
// With no module on the bus every cycle fails, and the core logs each failed
// transfer. After this many failures in a row, drop to one quiet presence
// check every ABSENT_MS until the module answers again.
constexpr uint32_t ABSENT_AFTER = 3000 / PERIOD_MS;
constexpr uint32_t ABSENT_MS    = 10000;

bool absent = false;

Stats st;
#if defined(UMBRAVOX_SCAN_ENGINE)
uint32_t cycleStart = 0;
constexpr uint32_t CYCLE_BUDGET_MS = 40;
bool transactionTime() {
    const uint16_t left = i2cbudget::remaining(cycleStart, millis(), CYCLE_BUDGET_MS);
    if (!left) { ++st.budgetExhausted; return false; }
    Wire.setTimeOut(left);
    return true;
}
#else
bool transactionTime() { return true; }
#endif

void put32le(uint8_t* b, uint32_t v) {
    b[0] = (uint8_t)v; b[1] = (uint8_t)(v >> 8);
    b[2] = (uint8_t)(v >> 16); b[3] = (uint8_t)(v >> 24);
}
uint32_t get32le(const uint8_t* b) {
    return b[0] | (b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

uint8_t endWrite(uint8_t addr) {
    const uint8_t error = Wire.endTransmission();
    if (error) {
        ++st.writeFailures[addr == CR_ADDR ? 0 : addr == RD_ADDR ? 1 : 2];
        st.lastWriteError = error;
    }
    return error;
}

uint8_t writeCr(uint32_t reg, uint32_t len) {   // 0 = ok, else Wire's error
    if (!transactionTime()) return 5;
    uint8_t b[8];
    put32le(b, reg);
    put32le(b + 4, len);
    Wire.beginTransmission(CR_ADDR);
    Wire.write(b, sizeof b);
    return endWrite(CR_ADDR);
}

size_t readBytes(uint8_t* buf, size_t n) {
    if (!transactionTime()) return 0;
    const size_t got = Wire.requestFrom(RD_ADDR, n);
    if (got != n) ++st.shortReads;
    for (size_t i = 0; i < got; i++) buf[i] = (uint8_t)Wire.read();
    return got;
}

uint8_t dummy(uint8_t addr) {   // 1-byte write; 0 = ACKed
    if (!transactionTime()) return 5;
    Wire.beginTransmission(addr);
    Wire.write((uint8_t)0);
    return endWrite(addr);
}

// The C5 bus quirk touch.cpp also works around: re-open once before a
// transaction (never between its steps), with the long timeout the module's
// clock stretching needs. Returns the timeout to put back afterwards.
uint16_t openBus() {
#if defined(UMBRAVOX_SCAN_ENGINE)
    cycleStart = millis();
#endif
    const uint16_t saved = Wire.getTimeOut();
    Wire.end();
    Wire.begin(board::PIN_SDA, board::PIN_SCL, 100000u);
    Wire.setTimeOut(TIMEOUT_MS);
    return saved;
}

// Bytes delivered, 0 if nothing was queued, -1 on a bus failure part way
// through, -2 if nothing answered at 0x50 at all.
int poll(void (*sink)(char, uint32_t), uint32_t now) {
    static uint8_t buf[CHUNK];
    int out = -1;
    {
    wsbus::Guard guard;
    const uint16_t savedTo = openBus();
    do {
        delay(STEP_MS);
        if (dummy(CR_ADDR) != 0) {
            // Demo Recovery_I2C: nudge the read and write addresses. Not
            // while absent -- with nothing there it only adds failures.
            if (!absent && (dummy(RD_ADDR) == 0 || dummy(WR_ADDR) == 0)) st.recov++;
            out = -2;
            break;
        }
        delay(STEP_MS);
        if (writeCr(0xAA510008UL, 4) != 0) break;
        delay(STEP_MS);
        uint8_t lb[4];
        if (readBytes(lb, 4) != 4) break;
        uint32_t len = get32le(lb);
        if (len == 0) { out = 0; break; }
        if (len >= 35 * 1024) break;           // the demo treats this as illegal
        if (len > CHUNK) len = CHUNK;           // the rest drains next cycle
        delay(STEP_MS);
        if (writeCr(0xAA512000UL, len) != 0) break;
        delay(STEP_MS);
        if (readBytes(buf, len) != len) break;
        out = (int)len;
    } while (0);
    Wire.setTimeOut(savedTo);
    } // parser runs without bus ownership; the touch task can now sample
    for (int i = 0; i < out; i++) sink((char)buf[i], now);
    return out;
}

// Demo Ql_Wake_I2C: the module's I2C side can doze after a while idle.
bool wake() {
    wsbus::Guard guard;
    bool ok = false;
    const uint16_t savedTo = openBus();
    do {
        delay(STEP_MS); if (dummy(CR_ADDR) != 0) break;
        delay(STEP_MS); if (writeCr(0xAA510004UL, 4) != 0) break;
        delay(STEP_MS);
        uint8_t fb[4];
        if (readBytes(fb, 4) != 4) break;
        if (get32le(fb) < 1) break;
        delay(STEP_MS); if (writeCr(0xAA531000UL, 1) != 0) break;
        delay(STEP_MS); ok = (dummy(WR_ADDR) == 0);
    } while (0);
    Wire.setTimeOut(savedTo);
    return ok;
}

} // namespace

void begin() {
    Wire.setBufferSize(CHUNK + 16);   // survives the Wire.end()/begin() re-opens
    Serial.println("gps: LC76G over I2C (0x50/0x54) on SDA 27 / SCL 26, polled");
}

void tick(void (*sink)(char, uint32_t), uint32_t now) {
    static uint32_t lastAt = 0, emptyRun = 0, failRun = 0;
    static bool     heard = false;
    if (lastAt && now - lastAt < (absent ? ABSENT_MS : PERIOD_MS)) return;
    lastAt = now ? now : 1;

    const int r = poll(sink, now);
    st.cycles++;
    if (r == -2) {                         // nobody at 0x50
        st.fails++;
        if (!absent && ++failRun >= ABSENT_AFTER) {
            absent = true;
            Serial.printf("gps: LC76G not answering on I2C -- checking every %lu s\n",
                          (unsigned long)(ABSENT_MS / 1000));
        }
        return;
    }
    failRun = 0;                           // it answered, whatever came next
    if (absent) {
        absent = false;
        Serial.println("gps: LC76G answering on I2C");
    }
    if (r < 0) { st.fails++; return; }     // answered, then a step failed
    if (r == 0) {
        st.empty++;
        if (++emptyRun >= WAKE_AFTER) {
            emptyRun = 0;
            if (wake()) st.wakes++;
        }
    } else {
        emptyRun = 0;
        st.bytes += (uint32_t)r;
        if (!heard) {
            heard = true;
            Serial.printf("gps: first NMEA from the LC76G, %d bytes, cycle %lu\n",
                          r, (unsigned long)st.cycles);
        }
    }
}

const Stats& stats() { return st; }

} // namespace gpsi2c
#endif // WS_C5_35
