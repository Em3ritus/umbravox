// Parsing a BLE advertisement, lifted out of blescan.cpp so the bounds can be
// host-tested without a radio (same pattern as csv.h / chanplan.h).
//
// Every length byte in an advertisement is attacker-controlled -- the payload
// comes straight off the air -- so the walk is the one place a crafted frame
// could push a read past the buffer. It is kept pure and fuzzed against
// malformed input in sim/test_ble_ad.cpp.
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace ble_ad {

// The fields a signature needs out of an advertisement. `name` points into the
// payload and is NOT null-terminated; copy `nameLen` bytes (the caller does,
// sanitising as it goes).
struct Fields {
    uint16_t       svc     = 0;        // first 16-bit service UUID (AD 0x02/0x03)
    uint16_t       company = 0;        // SIG company id (AD 0xFF manufacturer data)
    const uint8_t* name    = nullptr;  // local name bytes (AD 0x09, or 0x08)
    uint8_t        nameLen = 0;
};

// Walk the length-prefixed AD structures. An AD structure is [len][type][value]
// where len counts type+value, so it occupies payload indices i .. i+len and
// its last value byte sits at index i+len. That byte must be in range, so the
// walk proceeds only while i+len < pn; a structure that claims to reach the
// payload end or beyond (i+len >= pn) is malformed and stops the walk rather
// than reading past the buffer. (The earlier bound, i+len >= pn+1, let a
// structure end exactly at pn and read one byte past the payload.)
inline Fields scan(const uint8_t* pl, size_t pn) {
    Fields f;
    for (size_t i = 0; pl && i + 1 < pn; ) {
        const uint8_t len = pl[i];
        if (len == 0 || i + (size_t)len >= pn) break;   // malformed / would overrun
        const uint8_t  type = pl[i + 1];
        const uint8_t* val  = pl + i + 2;
        const uint8_t  vlen = (uint8_t)(len - 1);

        // 0x02/0x03: incomplete/complete list of 16-bit service UUIDs.
        if ((type == 0x02 || type == 0x03) && vlen >= 2 && !f.svc)
            f.svc = (uint16_t)(val[0] | (val[1] << 8));
        // 0xFF: manufacturer specific, opening with the SIG company id.
        else if (type == 0xFF && vlen >= 2 && !f.company)
            f.company = (uint16_t)(val[0] | (val[1] << 8));
        // 0x09 complete local name, 0x08 shortened. Prefer the complete one,
        // but take a short name over nothing.
        else if ((type == 0x09 || (type == 0x08 && !f.name)) && vlen) {
            f.name = val;
            f.nameLen = vlen;
        }

        i += (size_t)len + 1;
    }
    return f;
}

} // namespace ble_ad
