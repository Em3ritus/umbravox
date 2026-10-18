// GPS probe, two stages.
//
// Stage 1 measures the line instead of guessing at it. A UART idles high; the
// shortest pulse on a driven line is one bit time, so 1/width is the baud.
// That answers three questions at once -- is anything connected, is anything
// powered, and what rate is it running -- without a baud sweep.
//
// A pull-up is the control. If a pin still shows traffic with the pull-up on,
// something is actively driving it. If it goes quiet, the "data" was a
// floating input picking up noise, which is exactly what a sparse, $-less
// byte count looks like.
//
// Stage 2 only runs if stage 1 found a real signal.
#include <Arduino.h>

namespace {

const int PINS[] = {4, 5};
constexpr int N_PINS = sizeof(PINS) / sizeof(PINS[0]);
constexpr uint32_t MEASURE_US = 2000000;
constexpr uint32_t WARMUP_MS  = 6000;

const int STD_BAUD[] = {4800, 9600, 19200, 38400, 57600, 115200, 230400, 460800};
constexpr int N_STD = sizeof(STD_BAUD) / sizeof(STD_BAUD[0]);

struct Line {
    int      pin;
    uint32_t transitions;
    uint32_t minPulseUs;
    uint32_t highUs, lowUs;   // time spent at each level, for the idle check
};

Line measure(int pin) {
    Line L{pin, 0, 0xFFFFFFFFu, 0, 0};
    pinMode(pin, INPUT_PULLUP);
    delay(20);

    const uint32_t t0 = micros();
    int last = digitalRead(pin);
    uint32_t lastT = t0;

    while ((uint32_t)(micros() - t0) < MEASURE_US) {
        const int v = digitalRead(pin);
        if (v == last) continue;
        const uint32_t now = micros();
        const uint32_t w = now - lastT;
        if (last) L.highUs += w; else L.lowUs += w;
        // Sub-microsecond readings are sampling jitter, not bit times.
        if (w >= 2 && w < L.minPulseUs) L.minPulseUs = w;
        lastT = now;
        last  = v;
        L.transitions++;
    }
    if (L.minPulseUs == 0xFFFFFFFFu) L.minPulseUs = 0;
    return L;
}

int nearestBaud(uint32_t minPulseUs) {
    if (!minPulseUs) return 0;
    const long est = 1000000L / (long)minPulseUs;
    int best = STD_BAUD[0];
    long bestD = labs(est - STD_BAUD[0]);
    for (int i = 1; i < N_STD; i++) {
        const long d = labs(est - STD_BAUD[i]);
        if (d < bestD) { bestD = d; best = STD_BAUD[i]; }
    }
    // Only trust it if it is within 35% of a standard rate.
    return (bestD * 100 / best) < 35 ? best : 0;
}

HardwareSerial gps(1);
char line[100]; int lineN = 0;
int fixQual = 0, satsUsed = 0, satsView = 0;
char utc[12] = "--", lat[16] = "--", lon[16] = "--";
uint32_t sentences = 0, lastReport = 0;
int gpsRx = -1, gpsTx = -1, gpsBaud = 0;

void field(const char* s, int want, char* out, size_t n) {
    int f = 0; size_t o = 0; out[0] = 0;
    for (const char* p = s; *p; p++) {
        if (*p == ',') { if (++f > want) break; continue; }
        if (f == want && o + 1 < n) out[o++] = *p;
    }
    out[o] = 0;
}

void handleLine() {
    if (lineN < 7 || line[0] != '$') return;
    sentences++;
    char tmp[16];
    if (!strncmp(line + 3, "GGA", 3)) {
        field(line, 6, tmp, sizeof tmp); fixQual  = atoi(tmp);
        field(line, 7, tmp, sizeof tmp); satsUsed = atoi(tmp);
        field(line, 1, utc, sizeof utc);
        field(line, 2, lat, sizeof lat);
        field(line, 4, lon, sizeof lon);
    } else if (!strncmp(line + 3, "GSV", 3)) {
        field(line, 3, tmp, sizeof tmp); satsView = atoi(tmp);
    }
}

} // namespace

void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println("\n=== GPS probe ===");
    Serial.printf("warming up %ums...\n", (unsigned)WARMUP_MS);
    delay(WARMUP_MS);

    Serial.println("\n-- stage 1: measuring the lines (pull-up on) --");
    Line best{0, 0, 0, 0, 0};
    int  bestBaud = 0;

    for (int i = 0; i < N_PINS; i++) {
        const Line L = measure(PINS[i]);
        const int b = nearestBaud(L.minPulseUs);
        const uint32_t total = L.highUs + L.lowUs;
        const int pctHigh = total ? (int)(L.highUs * 100 / total) : 100;

        Serial.printf("  GPIO %-2d : %6u transitions, min pulse %4uus, %3d%% high",
                      L.pin, (unsigned)L.transitions, (unsigned)L.minPulseUs, pctHigh);
        if (L.transitions == 0)      Serial.print("   idle (nothing driving it)");
        else if (b)                  Serial.printf("   -> ~%d baud", b);
        else                         Serial.print("   -> no coherent bit rate (noise)");
        Serial.println();

        if (b && L.transitions > best.transitions) { best = L; bestBaud = b; }
    }

    if (!bestBaud) {
        Serial.println("\nRESULT: no UART signal on either pin.");
        Serial.println("  With the pull-up on, a connected+powered receiver would");
        Serial.println("  show thousands of transitions and a clean bit rate.");
        Serial.println("  Check, in this order:");
        Serial.println("   1. module power -- does it have a lit/blinking LED?");
        Serial.println("   2. is it on the GPS header (GPIO 4/5), not another connector?");
        Serial.println("   3. TX/RX swapped at the module end");
        Serial.println("   4. ground connected");
        Serial.println("Halting; reflash to retry.");
        for (;;) delay(1000);
    }

    gpsRx = best.pin;
    gpsTx = (best.pin == 4) ? 5 : 4;
    gpsBaud = bestBaud;
    Serial.printf("\n-- stage 2: reading GPIO %d at %d baud --\n", gpsRx, gpsBaud);
    gps.begin(gpsBaud, SERIAL_8N1, gpsRx, gpsTx);
    Serial.println("Cold start can take minutes, and it needs sky.\n");
}

void loop() {
    while (gps.available()) {
        const char ch = (char)gps.read();
        if (ch == '\n' || ch == '\r') {
            if (lineN) { line[lineN] = 0; handleLine(); lineN = 0; }
        } else if (lineN < (int)sizeof(line) - 1) {
            line[lineN++] = ch;
        }
    }
    if (millis() - lastReport >= 3000) {
        lastReport = millis();
        const char* q = fixQual == 0 ? "NO FIX" : fixQual == 1 ? "GPS fix"
                      : fixQual == 2 ? "DGPS fix" : "fix";
        Serial.printf("[%5us] %-8s used %2d  view %2d  utc %-10s %s %s  (%u sentences)\n",
                      (unsigned)(millis()/1000), q, satsUsed, satsView, utc,
                      lat, lon, (unsigned)sentences);
    }
}
