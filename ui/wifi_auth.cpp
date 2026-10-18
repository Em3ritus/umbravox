#include "wifi_auth.h"

namespace ui {
namespace {

// Suite selectors are four bytes: a three-byte OUI and a type. 00-0F-AC is
// the IEEE's own, used by RSN; 00-50-F2 is Microsoft's, used by WPA1.
bool ouiIs(const uint8_t* p, uint8_t a, uint8_t b, uint8_t c) {
    return p[0] == a && p[1] == b && p[2] == c;
}

void cipherBits(uint8_t type, uint16_t& bits) {
    if (type == 2) bits |= AUTH_TKIP;
    if (type == 4) bits |= AUTH_CCMP;
    // 8 and 9 are GCMP; WiGLE has no distinct token, and they imply the same
    // "modern AES" story CCMP does.
    if (type == 8 || type == 9) bits |= AUTH_CCMP;
}

void akmBits(uint8_t type, uint16_t& bits) {
    switch (type) {
        case 1: case 3: case 5: case 11: case 12: bits |= AUTH_EAP; break;
        case 2: case 6:                           bits |= AUTH_PSK; break;
        case 8: case 9:                           bits |= AUTH_SAE; break;
        case 18:                                  bits |= AUTH_OWE; break;
        default: break;
    }
}

// Shared shape of the RSN element and the WPA vendor element: a version,
// a group cipher, a counted list of pairwise ciphers, then a counted list
// of AKM suites. Every length is attacker-controlled, so every step is
// bounds-checked against what is actually left.
void parseSuites(const uint8_t* p, int n, uint8_t o0, uint8_t o1, uint8_t o2,
                 uint16_t& bits) {
    // Skip the group cipher without recording it. WiGLE's AuthMode reports
    // the *pairwise* cipher -- what actually encrypts unicast traffic -- and
    // the two differ on any mixed-mode AP, which commonly pairs a TKIP group
    // with CCMP pairwise for legacy clients.
    int i = 2 + 4;                               // version, group cipher

    if (i + 2 > n) return;
    const int nPair = p[i] | (p[i + 1] << 8);
    i += 2;
    for (int k = 0; k < nPair; k++) {
        if (i + 4 > n) return;
        if (ouiIs(p + i, o0, o1, o2)) cipherBits(p[i + 3], bits);
        i += 4;
    }

    if (i + 2 > n) return;
    const int nAkm = p[i] | (p[i + 1] << 8);
    i += 2;
    for (int k = 0; k < nAkm; k++) {
        if (i + 4 > n) return;
        if (ouiIs(p + i, o0, o1, o2)) akmBits(p[i + 3], bits);
        i += 4;
    }
}

void add(char* out, size_t n, size_t& o, const char* s) {
    for (int i = 0; s[i] && o + 1 < n; i++) out[o++] = s[i];
    out[o] = 0;
}

} // namespace

uint16_t parseAuth(const uint8_t* body, int len) {
    if (!body || len < 12) return 0;
    uint16_t bits = 0;

    // Fixed parameters: 8 bytes timestamp, 2 interval, 2 capability.
    const uint16_t cap = (uint16_t)(body[10] | (body[11] << 8));
    if (cap & 0x0001) bits |= AUTH_ESS;
    if (cap & 0x0002) bits |= AUTH_IBSS;
    if (cap & 0x0010) bits |= AUTH_PRIVACY;

    for (int i = 12; i + 2 <= len; ) {
        const uint8_t tag = body[i];
        const uint8_t tl  = body[i + 1];
        const uint8_t* v  = body + i + 2;
        if (i + 2 + tl > len) break;             // truncated element

        if (tag == 48 && tl >= 2) {              // RSN
            bits |= AUTH_WPA2;
            parseSuites(v, tl, 0x00, 0x0F, 0xAC, bits);
        } else if (tag == 221 && tl >= 6 &&
                   ouiIs(v, 0x00, 0x50, 0xF2) && v[3] == 1) {
            bits |= AUTH_WPA;                    // vendor WPA1
            parseSuites(v + 4, tl - 4, 0x00, 0x50, 0xF2, bits);
        }
        i += 2 + tl;
    }

    if (bits & AUTH_SAE) bits |= AUTH_WPA3;
    // Privacy with neither RSN nor a WPA element is the old thing.
    if ((bits & AUTH_PRIVACY) && !(bits & (AUTH_WPA | AUTH_WPA2))) bits |= AUTH_WEP;
    return bits;
}

size_t authString(uint16_t bits, char* out, size_t n) {
    size_t o = 0;
    if (!out || n == 0) return 0;
    out[0] = 0;

    // WiGLE lists each scheme the AP offers, strongest naming last, then the
    // mode. A transition-mode AP genuinely advertises two, and saying so is
    // more honest than picking one.
    if (bits & AUTH_WPA) {
        add(out, n, o, "[WPA-");
        add(out, n, o, (bits & AUTH_PSK) ? "PSK-" : (bits & AUTH_EAP) ? "EAP-" : "");
        add(out, n, o, (bits & AUTH_TKIP) ? "TKIP]" : "CCMP]");
    }
    if (bits & AUTH_WPA2) {
        const bool sae = (bits & AUTH_SAE) != 0;
        add(out, n, o, sae ? "[WPA3-" : "[WPA2-");
        if (sae)                  add(out, n, o, "SAE-");
        else if (bits & AUTH_OWE) add(out, n, o, "OWE-");
        else if (bits & AUTH_PSK) add(out, n, o, "PSK-");
        else if (bits & AUTH_EAP) add(out, n, o, "EAP-");
        add(out, n, o, (bits & AUTH_CCMP) ? "CCMP]" : "TKIP]");
    }
    if (bits & AUTH_WEP) add(out, n, o, "[WEP]");
    if (!(bits & (AUTH_WPA | AUTH_WPA2 | AUTH_WEP))) add(out, n, o, "[OPEN]");

    if (bits & AUTH_ESS)  add(out, n, o, "[ESS]");
    if (bits & AUTH_IBSS) add(out, n, o, "[IBSS]");
    return o;
}

const char* authShort(uint16_t bits) {
    if (bits & AUTH_WPA3) return "WPA3";
    if (bits & AUTH_WPA2) return "WPA2";
    if (bits & AUTH_WPA)  return "WPA";
    if (bits & AUTH_WEP)  return "WEP";
    return "OPEN";
}

} // namespace ui
