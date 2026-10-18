// What a network's beacon says about how it is secured.
//
// Until now a beacon told this firmware one bit: Privacy, set or not. WiGLE
// wants the whole thing -- WPA2-PSK-CCMP, WPA3-SAE, an open network -- and
// that lives in the RSN and vendor-WPA information elements, not in the
// capability field.
//
// Parsed into bits so an entry costs two bytes in a table of thousands, and
// rendered as WiGLE's capability string only on export.
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace ui {

enum AuthBits : uint16_t {
    AUTH_ESS     = 0x0001,   // infrastructure AP
    AUTH_IBSS    = 0x0002,   // ad-hoc
    AUTH_PRIVACY = 0x0004,   // capability bit: encrypted, somehow
    AUTH_WEP     = 0x0008,   // privacy set with no RSN and no WPA IE
    AUTH_WPA     = 0x0010,   // vendor WPA element (WPA1)
    AUTH_WPA2    = 0x0020,   // RSN element
    AUTH_WPA3    = 0x0040,   // RSN with SAE
    AUTH_PSK     = 0x0080,
    AUTH_EAP     = 0x0100,
    AUTH_SAE     = 0x0200,
    AUTH_OWE     = 0x0400,
    AUTH_TKIP    = 0x0800,
    AUTH_CCMP    = 0x1000,
};

// `body` is the beacon body: the fixed parameters followed by the tagged
// elements, i.e. the frame from offset 24. `len` is what remains of it.
uint16_t parseAuth(const uint8_t* body, int len);

// WiGLE's AuthMode column, e.g. "[WPA2-PSK-CCMP][ESS]" or "[OPEN][ESS]".
// Writes at most n-1 characters plus a terminator.
size_t authString(uint16_t bits, char* out, size_t n);

// Short form for a 160px screen: "WPA2", "WPA3", "WEP", "OPEN".
const char* authShort(uint16_t bits);

} // namespace ui
