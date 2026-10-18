#include "signatures.h"

// Signature tables derived from SquachWatch-CYD's src/signatures.cpp
// (GPL-3.0). See NOTICE for attribution and the changes made.
//
// The confidence column is the valuable part and the reason this is a port
// rather than a fresh list. Of 97 OUI rows, 60 are LOW -- and they are not
// mistakes. Flock really does build on ESP32, so an Espressif prefix really
// is evidence; it is just evidence shared with every dev board on earth.
// Recording that honestly is the difference between a detector and a rumour,
// and it is what the ALERT floor on ATTUNE exists to filter.

namespace ui {
namespace {

struct SsidSig { const char* prefix; uint8_t threat, conf; const char* why; };
struct UuidSig { uint16_t uuid;      uint8_t threat, conf; const char* why; };
struct CompSig { uint16_t company;   uint8_t threat, conf; const char* why; };
struct OuiSig  { uint8_t oui[3];     uint8_t threat, conf; const char* why; };

// --- WiFi SSID prefixes (case-insensitive) ------------------------------
// A device that names itself is making a claim about what it is, which is
// stronger evidence than the chip it happens to be built on.
const SsidSig SSIDS[] = {
    {"AB2-", TH_BODYCAM, CONF_HIGH, "AXON-BODY2"},
    {"AB3-", TH_BODYCAM, CONF_HIGH, "AXON-BODY3"},
    {"AB4-", TH_BODYCAM, CONF_HIGH, "AXON-BODY4"},
    {"AXON-", TH_BODYCAM, CONF_HIGH, "AXON-FIELD"},
    {"flock-", TH_FLOCK, CONF_MED, "FLOCK-SETUP"},
    {"Pineapple_", TH_PENTEST, CONF_HIGH, "PINEAPPLE"},
    {"pwned", TH_PENTEST, CONF_MED, "DEAUTHER"},
};

// --- WiFi OUIs ----------------------------------------------------------
const OuiSig OUIS[] = {
    {{0x00, 0x25, 0xDF}, TH_BODYCAM, CONF_HIGH, "AXON"},
    {{0xE4, 0x05, 0x40}, TH_BODYCAM, CONF_LOW, "AXON-BODY"},
    {{0x28, 0x24, 0xFF}, TH_BODYCAM, CONF_LOW, "AXON-SIGNAL"},
    {{0x24, 0x0A, 0xC4}, TH_FLOCK, CONF_LOW, "FLOCK-ESP32"},
    {{0x30, 0xAE, 0xA4}, TH_FLOCK, CONF_LOW, "FLOCK-ESP32"},
    {{0x24, 0x6F, 0x28}, TH_FLOCK, CONF_LOW, "FLOCK-ESP32"},
    {{0xCC, 0x50, 0xE3}, TH_FLOCK, CONF_LOW, "FLOCK-ESP32"},
    {{0xDC, 0x54, 0x75}, TH_FLOCK, CONF_LOW, "FLOCK-ESP32"},
    {{0xE8, 0x9F, 0x6D}, TH_FLOCK, CONF_LOW, "FLOCK-ESP32"},
    {{0x8C, 0xAA, 0xB5}, TH_FLOCK, CONF_LOW, "FLOK-ESP-S3"},
    {{0x34, 0x85, 0x18}, TH_FLOCK, CONF_LOW, "FLOK-ESP-S3"},
    {{0xB4, 0x1E, 0x52}, TH_FLOCK, CONF_HIGH, "FLOCK-MA-L"},
    {{0xD4, 0xAD, 0xFC}, TH_FLOCK, CONF_LOW, "FLOCK-ESP32"},
    {{0xAC, 0x67, 0xB2}, TH_FLOCK, CONF_LOW, "FLOCK-ESP32"},
    {{0x84, 0xF3, 0xEB}, TH_FLOCK, CONF_LOW, "FLOK-ESP-S3"},
    {{0xB4, 0xE6, 0x2D}, TH_FLOCK, CONF_LOW, "FLOCK-ESP32"},
    {{0xCC, 0xDB, 0xA7}, TH_FLOCK, CONF_LOW, "FLOCK-ESP32"},
    {{0x94, 0xB9, 0x7E}, TH_FLOCK, CONF_LOW, "FLOCK-ESP32"},
    {{0xA4, 0xCF, 0x12}, TH_FLOCK, CONF_LOW, "FLOK-ESP-S2"},
    {{0xC0, 0x49, 0xEF}, TH_FLOCK, CONF_LOW, "FLOK-ESP-C6"},
    {{0x24, 0xB2, 0xB9}, TH_FLOCK, CONF_LOW, "FLOCK-LITEO"},
    {{0xD0, 0x39, 0x57}, TH_FLOCK, CONF_LOW, "FLOCK"},
    {{0x00, 0xF4, 0x8D}, TH_FLOCK, CONF_LOW, "FLOCK"},
    {{0x14, 0x5A, 0xFC}, TH_FLOCK, CONF_LOW, "FLOCK"},
    {{0x80, 0x30, 0x49}, TH_FLOCK, CONF_LOW, "FLOCK"},
    {{0xE0, 0x0A, 0xF6}, TH_FLOCK, CONF_LOW, "FLOCK"},
    {{0x70, 0xC9, 0x4E}, TH_FLOCK, CONF_LOW, "FLOCK"},
    {{0x3C, 0x91, 0x80}, TH_FLOCK, CONF_LOW, "FLOCK"},
    {{0xD8, 0xF3, 0xBC}, TH_FLOCK, CONF_LOW, "FLOCK"},
    {{0xB8, 0x35, 0x32}, TH_FLOCK, CONF_LOW, "FLOCK"},
    {{0x82, 0x6B, 0xF2}, TH_FLOCK, CONF_LOW, "FLOCK-DEFLK"},
    {{0x74, 0x4C, 0xA1}, TH_FLOCK, CONF_LOW, "FLOCK-LITEO"},
    {{0x9C, 0x2F, 0x9D}, TH_FLOCK, CONF_LOW, "FLOCK-LITEO"},
    {{0xC0, 0x35, 0x32}, TH_FLOCK, CONF_LOW, "FLOCK-LITEO"},
    {{0x94, 0x08, 0x53}, TH_FLOCK, CONF_LOW, "FLOCK-LITEO"},
    {{0xE4, 0xAA, 0xEA}, TH_FLOCK, CONF_LOW, "FLOCK-LITEO"},
    {{0xF4, 0x6A, 0xDD}, TH_FLOCK, CONF_LOW, "FLOCK-LITEO"},
    {{0xE8, 0xD0, 0xFC}, TH_FLOCK, CONF_LOW, "FLOCK-LITEO"},
    {{0xB8, 0x1E, 0xA4}, TH_FLOCK, CONF_LOW, "FLOCK-LITEO"},
    {{0x70, 0x08, 0x94}, TH_FLOCK, CONF_LOW, "FLOCK-LITEO"},
    {{0x58, 0x00, 0xE3}, TH_FLOCK, CONF_LOW, "FLOCK-LITEO"},
    {{0x5C, 0x93, 0xA2}, TH_FLOCK, CONF_LOW, "FLOCK-LITEO"},
    {{0x64, 0x6E, 0x69}, TH_FLOCK, CONF_LOW, "FLOCK-LITEO"},
    {{0x14, 0xB5, 0xCD}, TH_FLOCK, CONF_LOW, "FLOCK-LITEO"},
    {{0x08, 0x3A, 0x88}, TH_FLOCK, CONF_LOW, "FLOCK-UGSI"},
    {{0xE0, 0x4F, 0x43}, TH_FLOCK, CONF_LOW, "FLOCK-UGSI"},
    {{0x58, 0x8E, 0x81}, TH_FLOCK, CONF_LOW, "FLOCK-SILAB"},
    {{0xEC, 0x1B, 0xBD}, TH_FLOCK, CONF_LOW, "FLOCK-SILAB"},
    {{0x90, 0x35, 0xEA}, TH_FLOCK, CONF_LOW, "FLOCK-SILAB"},
    {{0x3C, 0x71, 0xBF}, TH_FLOCK, CONF_LOW, "FLOCK-ESP32"},
    {{0x00, 0x03, 0x7F}, TH_FLOCK, CONF_LOW, "FLOCK-QCA"},
    {{0x00, 0xA0, 0xD8}, TH_FLOCK, CONF_LOW, "FLOCK-OEM"},
    {{0x00, 0x04, 0x7D}, TH_ALPR, CONF_HIGH, "ALPR-MTRLA"},
    {{0x00, 0x18, 0x85}, TH_ALPR, CONF_HIGH, "ALPR-MTRLA"},
    {{0x00, 0x1F, 0x92}, TH_ALPR, CONF_HIGH, "ALPR-MTRLA"},
    {{0x4C, 0xCC, 0x34}, TH_ALPR, CONF_HIGH, "ALPR-MTRLA"},
    {{0xB8, 0xE2, 0x8C}, TH_ALPR, CONF_HIGH, "ALPR-MTRLA"},
    {{0x00, 0xBF, 0x15}, TH_ALPR, CONF_HIGH, "ALPR-GENTEC"},
    {{0x0C, 0xBF, 0x15}, TH_ALPR, CONF_HIGH, "ALPR-GENTEC"},
    {{0x20, 0x13, 0x00}, TH_SKIMMER, CONF_LOW, "SKIM-LINVOR"},
    {{0x98, 0xD3, 0x00}, TH_SKIMMER, CONF_LOW, "SKIM-SPP"},
    {{0x00, 0x1A, 0x7D}, TH_SKIMMER, CONF_LOW, "SKIM-CSR"},
    {{0x2C, 0xAA, 0x8E}, TH_CAMERA, CONF_HIGH, "WYZE"},
    {{0xD0, 0x3F, 0x27}, TH_CAMERA, CONF_HIGH, "WYZE"},
    {{0x7C, 0x78, 0xB2}, TH_CAMERA, CONF_HIGH, "WYZE"},
    {{0xB8, 0xD7, 0xAF}, TH_CAMERA, CONF_LOW, "WYZE-MOD"},
    {{0x34, 0xD2, 0x70}, TH_CAMERA, CONF_MED, "AMAZON"},
    {{0xF0, 0x27, 0x2D}, TH_CAMERA, CONF_MED, "AMAZON"},
    {{0xC0, 0x56, 0xE3}, TH_CAMERA, CONF_HIGH, "HIKVISION"},
    {{0x44, 0x19, 0xB6}, TH_CAMERA, CONF_HIGH, "HIKVISION"},
    {{0x28, 0x57, 0xBE}, TH_CAMERA, CONF_HIGH, "HIKVISION"},
    {{0x00, 0xE0, 0x4C}, TH_CAMERA, CONF_LOW, "REALTEK"},
    {{0xBC, 0xDD, 0xC2}, TH_CAMERA, CONF_LOW, "ARLO"},
    {{0x4C, 0x69, 0x05}, TH_CAMERA, CONF_LOW, "BLINK"},
    {{0xA4, 0xC1, 0x38}, TH_CAMERA, CONF_LOW, "TUYA"},
    {{0xFC, 0x65, 0xDE}, TH_CAMERA, CONF_MED, "RING"},
    {{0x68, 0x37, 0xE9}, TH_CAMERA, CONF_MED, "RING"},
    {{0xAC, 0x9F, 0xC3}, TH_CAMERA, CONF_HIGH, "RING"},
    {{0x18, 0x7F, 0x88}, TH_CAMERA, CONF_HIGH, "RING"},
    {{0x34, 0x3E, 0xA4}, TH_CAMERA, CONF_HIGH, "RING"},
    {{0x54, 0xE0, 0x19}, TH_CAMERA, CONF_HIGH, "RING"},
    {{0x5C, 0x47, 0x5E}, TH_CAMERA, CONF_HIGH, "RING"},
    {{0x64, 0x9A, 0x63}, TH_CAMERA, CONF_HIGH, "RING"},
    {{0x90, 0x48, 0x6C}, TH_CAMERA, CONF_HIGH, "RING"},
    {{0x9C, 0x76, 0x13}, TH_CAMERA, CONF_HIGH, "RING"},
    {{0xCC, 0x3B, 0xFB}, TH_CAMERA, CONF_HIGH, "RING"},
    {{0xC4, 0xDB, 0xAD}, TH_CAMERA, CONF_HIGH, "RING"},
    {{0x24, 0x2B, 0xD6}, TH_CAMERA, CONF_HIGH, "RING"},
    {{0x00, 0xB4, 0x63}, TH_CAMERA, CONF_HIGH, "RING"},
    {{0x50, 0xE4, 0x67}, TH_CAMERA, CONF_HIGH, "RING"},
    {{0xE0, 0xA7, 0x00}, TH_CAMERA, CONF_HIGH, "VERKADA"},
    {{0x70, 0x1A, 0xD5}, TH_CAMERA, CONF_HIGH, "AVIGILON"},
    {{0x00, 0x40, 0x8C}, TH_CAMERA, CONF_HIGH, "AXIS"},
    {{0xB8, 0xA4, 0x4F}, TH_CAMERA, CONF_HIGH, "AXIS"},
    {{0x0C, 0xFA, 0x22}, TH_PENTEST, CONF_HIGH, "FLIPPER"},
    {{0x02, 0xC0, 0xCA}, TH_PENTEST, CONF_LOW, "HAK5-LA"},
    {{0x02, 0x13, 0x37}, TH_PENTEST, CONF_LOW, "HAK5-LA"},
};

// --- BLE 16-bit service UUIDs -------------------------------------------
const UuidSig UUIDS[] = {
    {0xFEED, TH_TRACKER, CONF_HIGH, "TILE"},
    {0xFEEC, TH_TRACKER, CONF_HIGH, "TILE"},
    {0xFD5F, TH_GLASSES, CONF_HIGH, "RAYBANMETA"},
    {0x3100, TH_RAVEN, CONF_MED, "RAVEN"},
    {0x3200, TH_RAVEN, CONF_MED, "RAVEN"},
    {0x3300, TH_RAVEN, CONF_MED, "RAVEN"},
    {0x3400, TH_RAVEN, CONF_MED, "RAVEN"},
    {0x3500, TH_RAVEN, CONF_MED, "RAVEN"},
    {0xFFFA, TH_DRONE, CONF_HIGH, "DRONEID"},
    {0xFD5A, TH_TRACKER, CONF_HIGH, "SMARTTAG"},
    {0xFEAA, TH_TRACKER, CONF_LOW, "FINDMYDEV"},
    {0x3081, TH_PENTEST, CONF_HIGH, "FLIPPER"},
    {0x3082, TH_PENTEST, CONF_HIGH, "FLIPPER"},
    {0x3083, TH_PENTEST, CONF_HIGH, "FLIPPER"},
};

// --- BLE SIG company identifiers ----------------------------------------
const CompSig COMPANIES[] = {
    {0x004C, TH_TRACKER, CONF_LOW, "APPLE"},
    {0x09C8, TH_FLOCK, CONF_MED, "XUNTONG"},
    {0x01AB, TH_GLASSES, CONF_MED, "META"},
    {0x058E, TH_GLASSES, CONF_MED, "META-TECH"},
    {0x0D53, TH_GLASSES, CONF_MED, "LUXOTTICA"},
    {0x03C2, TH_GLASSES, CONF_MED, "SNAP"},
    {0x0E29, TH_PENTEST, CONF_HIGH, "FLIPPER"},
};

bool startsWith(const char* s, const char* prefix) {
    if (!s || !prefix) return false;
    for (int i = 0; prefix[i]; i++) {
        char a = s[i], b = prefix[i];
        if (a >= 'a' && a <= 'z') a -= 32;         // compare case-insensitively
        if (b >= 'a' && b <= 'z') b -= 32;
        if (a != b) return false;
    }
    return true;
}

} // namespace

// No `default:` on purpose. With -Wswitch, adding a Threat and forgetting
// to name it becomes a build error instead of an alert card that draws a
// blank where the threat should be -- which is exactly what TH_DEAUTH and
// TH_ROGUE did when they were added to the enum and not to this table.
const char* threatName(uint8_t t) {
    switch ((Threat)t) {
        case TH_FLOCK:   return "FLOCK";
        case TH_ALPR:    return "ALPR";
        case TH_BODYCAM: return "BODYCAM";
        case TH_CAMERA:  return "CAMERA";
        case TH_GLASSES: return "GLASSES";
        case TH_TRACKER: return "TRACKER";
        case TH_SKIMMER: return "SKIMMER";
        case TH_DRONE:   return "DRONE";
        case TH_RAVEN:   return "RAVEN";
        case TH_PENTEST: return "PENTEST";
        case TH_DEAUTH:  return "DEAUTH";
        case TH_ROGUE:   return "ROGUE";
        case TH_FOLLOW:  return "FOLLOW";
        case TH_NONE:
        case TH_COUNT:   break;
    }
    return "";
}

const char* confName(uint8_t c) {
    switch (c) {
        case CONF_HIGH: return "HIGH";
        case CONF_MED:  return "MED";
        case CONF_LOW:  return "LOW";
        default:        return "OFF";
    }
}

Hit matchWifi(const uint8_t mac[6], const char* ssid) {
    Hit best;
    if (ssid && ssid[0]) {
        for (const SsidSig& s : SSIDS) {
            if (!startsWith(ssid, s.prefix)) continue;
            if (s.conf > best.conf || best.threat == TH_NONE) {
                best.threat = s.threat; best.conf = s.conf; best.why = s.why;
            }
        }
    }
    if (mac) {
        for (const OuiSig& o : OUIS) {
            if (mac[0] != o.oui[0] || mac[1] != o.oui[1] || mac[2] != o.oui[2]) continue;
            // Strongest evidence wins, so a named device is not demoted by a
            // weak registry match on the same frame.
            if (o.conf > best.conf || best.threat == TH_NONE) {
                best.threat = o.threat; best.conf = o.conf; best.why = o.why;
            }
        }
    }
    return best;
}

Hit matchBle(uint16_t svcUuid, uint16_t companyId) {
    Hit best;
    if (svcUuid) {
        for (const UuidSig& u : UUIDS) {
            if (u.uuid != svcUuid) continue;
            if (u.conf > best.conf || best.threat == TH_NONE) {
                best.threat = u.threat; best.conf = u.conf; best.why = u.why;
            }
        }
    }
    if (companyId) {
        for (const CompSig& c : COMPANIES) {
            if (c.company != companyId) continue;
            if (c.conf > best.conf || best.threat == TH_NONE) {
                best.threat = c.threat; best.conf = c.conf; best.why = c.why;
            }
        }
    }
    return best;
}

} // namespace ui
