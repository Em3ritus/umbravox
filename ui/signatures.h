// Signature matching: deciding that a radio you can see is a thing worth
// naming.
//
// The hard part is not matching, it is *not over-matching*. A MAC prefix
// identifies whoever registered the block with the IEEE, which for most of
// this hardware is the chip vendor, not the product vendor -- a camera built
// on an ESP32 shares its OUI with every hobby board on earth. Matching one
// and shouting "surveillance camera" is worse than saying nothing, because it
// trains you to ignore the alert.
//
// So every signature carries a confidence, and confidence is per *signature*,
// not per device type. The same threat can be HIGH from one piece of evidence
// and LOW from another, and the alert gate is a floor on that confidence.
#pragma once
#include <stdint.h>

namespace ui {

enum Threat : uint8_t {
    TH_NONE = 0,
    TH_FLOCK,       // Flock Safety cameras and sensors
    TH_ALPR,        // other plate readers: Motorola, Genetec
    TH_BODYCAM,     // Axon body cameras and LE equipment
    TH_CAMERA,      // fixed cameras, including Ring
    TH_GLASSES,     // camera glasses: Ray-Ban Meta, Snap
    TH_TRACKER,     // AirTag, Tile, SmartTag, Find My
    TH_SKIMMER,     // card skimmers
    TH_DRONE,       // Remote ID drones
    TH_RAVEN,       // gunshot detectors
    TH_PENTEST,     // Flipper, Pineapple, deauthers
    // Behavioural, not signature-matched: there is nothing to look up, only
    // a rate that is wrong and a pair of beacons that contradict each other.
    TH_DEAUTH,      // deauthentication / disassociation flood
    TH_ROGUE,       // one SSID, two BSSIDs disagreeing about security
    TH_FOLLOW,      // a tracker that has travelled with you
    TH_COUNT
};

// LOW is the default for anything resting on a shared identifier. Only
// evidence that is specific to the product earns HIGH.
enum Conf : uint8_t { CONF_LOW = 0, CONF_MED, CONF_HIGH, CONF_OFF };

struct Hit {
    uint8_t     threat = TH_NONE;
    uint8_t     conf   = CONF_LOW;
    const char* why    = "";     // which evidence fired, for the alert card
};

// A recorded hit, as the radios hand it to the UI.
constexpr int ALERT_NAME = 20;
struct AlertRec {
    uint8_t     threat = TH_NONE;
    uint8_t     conf   = CONF_LOW;
    const char* why    = "";
    char        name[ALERT_NAME] = {0};
    uint32_t    atMs   = 0;
};

const char* threatName(uint8_t t);
const char* confName(uint8_t c);

// Signature data derived from SquachWatch-CYD (GPL-3.0); see NOTICE.
//
// WiFi evidence: the OUI of the BSSID, and the SSID if the beacon carried one.
Hit matchWifi(const uint8_t mac[6], const char* ssid);

// BLE evidence: a 16-bit service UUID and/or a SIG company identifier.
// Pass 0 for either when it was not present.
Hit matchBle(uint16_t svcUuid, uint16_t companyId);

} // namespace ui
