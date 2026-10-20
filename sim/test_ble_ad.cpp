// Host fuzz-ish test for the BLE advertisement walk. Built under the address
// and UB sanitizers (see the Makefile): a read past the payload -- the class
// of bug this parser is the natural home for -- aborts the test rather than
// passing silently.
#include "../device/ble_ad.h"
#include <stdio.h>
#include <string.h>

static int fails = 0;
static void check(const char* what, bool ok) {
    printf(ok ? "  ok   %s\n" : "  FAIL %s\n", what);
    if (!ok) fails++;
}

// Copy the parsed name the way blescan does. This is the read an out-of-bounds
// nameLen would push past the payload, so doing it on a heap buffer lets
// AddressSanitizer catch a regressed bound.
static void copyName(const ble_ad::Fields& f) {
    char tmp[64]; int k = 0;
    for (; f.name && k < (int)sizeof(tmp) - 1 && k < f.nameLen; k++)
        tmp[k] = (char)f.name[k];
    tmp[k] = 0;
    (void)tmp;
}

int main() {
    // 1. Well-formed: flags, 16-bit service UUID, manufacturer data, name.
    {
        const uint8_t p[] = {
            0x02, 0x01, 0x06,                   // flags (ignored)
            0x03, 0x03, 0xED, 0xFE,             // service UUID 0xFEED
            0x05, 0xFF, 0x4C, 0x00, 0x12, 0x34, // mfr data, company 0x004C
            0x05, 0x09, 'T','i','l','e',        // complete local name "Tile"
        };
        ble_ad::Fields f = ble_ad::scan(p, sizeof p);
        check("service UUID parsed", f.svc == 0xFEED);
        check("company id parsed",   f.company == 0x004C);
        check("name parsed",         f.name && f.nameLen == 4 &&
                                     memcmp(f.name, "Tile", 4) == 0);
        copyName(f);
    }

    // 2. A shortened name (0x08) is taken when there is no complete one.
    {
        const uint8_t p[] = { 0x04, 0x08, 'F','S','x' };
        ble_ad::Fields f = ble_ad::scan(p, sizeof p);
        check("shortened name taken", f.name && f.nameLen == 3);
    }

    // 3. Null and tiny payloads are safe and yield nothing.
    {
        ble_ad::Fields f = ble_ad::scan(nullptr, 0);
        check("null payload -> empty", f.svc == 0 && f.company == 0 && !f.name);
        const uint8_t one = 0x02;
        f = ble_ad::scan(&one, 1);
        check("1-byte payload -> empty", !f.name && f.svc == 0);
    }

    // 4. A zero length byte stops the walk instead of spinning.
    {
        const uint8_t p[] = { 0x00, 0x09, 'X' };
        ble_ad::Fields f = ble_ad::scan(p, sizeof p);
        check("zero length stops the walk", !f.name);
    }

    // 5. The off-by-one: a structure whose declared length reaches exactly the
    //    payload end claims one byte it does not have, and must be rejected --
    //    not parsed into a name that reads past the buffer. Heap-allocated so
    //    ASan flags a regressed bound when copyName() reads it.
    {
        uint8_t* p = new uint8_t[4];
        p[0] = 4; p[1] = 0x09; p[2] = 'A'; p[3] = 'B';   // len 4 needs index 4
        ble_ad::Fields f = ble_ad::scan(p, 4);
        check("overrunning structure rejected", f.name == nullptr);
        copyName(f);                                      // a no-op under the fix
        delete[] p;
    }

    // 6. A valid structure followed by a truncated one: keep the valid field,
    //    drop the overrun.
    {
        uint8_t* p = new uint8_t[7];
        p[0] = 0x03; p[1] = 0x03; p[2] = 0xED; p[3] = 0xFE; // svc 0xFEED, ok
        p[4] = 0x09; p[5] = 0x09; p[6] = 'Z';               // name claims 9 bytes
        ble_ad::Fields f = ble_ad::scan(p, 7);
        check("valid field kept before truncated one", f.svc == 0xFEED);
        check("truncated name dropped",                f.name == nullptr);
        copyName(f);
        delete[] p;
    }

    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
