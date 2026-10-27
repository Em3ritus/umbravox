// Host test for the WiGLE row formatters (device/wiglerow.h), shared by the
// SD append path and the console WIGLE dump. Golden rows are copied verbatim
// from the user's field CSV (written by the old whole-file exporter), so the
// append logger must produce the same bytes for the same data.
#include "../device/wiglerow.h"
#include <stdio.h>
#include <string.h>

static int fails = 0;
static void check(const char* what, bool ok) {
    printf(ok ? "  ok   %s\n" : "  FAIL %s\n", what);
    if (!ok) fails++;
}

int main() {
    char buf[256];
    printf("WiFi rows match the field CSV byte for byte:\n");
    {
        const uint8_t m[6] = {0xB0, 0x39, 0x56, 0x1A, 0x25, 0x71};
        // 2026-10-23 18:37:21 UTC
        const int n = wiglerow::wifi(buf, sizeof buf, m, "NETGEAR79", "[WPA2-PSK-CCMP][ESS]",
                                     1792780641u, 4, -88, 420535600, -710599890);
        check("NETGEAR79 row", strcmp(buf, "B0:39:56:1A:25:71,\"NETGEAR79\",[WPA2-PSK-CCMP][ESS],"
              "2026-10-23 18:37:21,4,2427,-88,42.0535600,-71.0599890,0,30,,,WIFI\n") == 0);
        check("length returned", n == (int)strlen(buf));
    }
    {
        const uint8_t m[6] = {0x68, 0x8F, 0xC9, 0xF8, 0x16, 0x8A};
        wiglerow::wifi(buf, sizeof buf, m, "CARLINK-181EE1", "[WPA2-PSK-CCMP][ESS]",
                       1793044703u, 36, -80, 421017968, -710194615);
        check("5 GHz row (channel 36 -> 5180)", strcmp(buf, "68:8F:C9:F8:16:8A,\"CARLINK-181EE1\","
              "[WPA2-PSK-CCMP][ESS],2026-10-26 19:58:23,36,5180,-80,42.1017968,-71.0194615,0,30,,,WIFI\n") == 0);
    }
    {
        const uint8_t m[6] = {0xFA, 0x8F, 0xCA, 0x78, 0xBC, 0x6F};
        wiglerow::wifi(buf, sizeof buf, m, "GoogleHome8056.k,", "[OPEN][ESS]",
                       1793045046u, 6, -97, 421275143, -710094355);
        check("SSID with a comma stays quoted", strcmp(buf, "FA:8F:CA:78:BC:6F,\"GoogleHome8056.k,\","
              "[OPEN][ESS],2026-10-26 20:04:06,6,2437,-97,42.1275143,-71.0094355,0,30,,,WIFI\n") == 0);
    }
    {
        const uint8_t m[6] = {0x1E, 0x46, 0xA7, 0x6B, 0x37, 0xBA};
        wiglerow::wifi(buf, sizeof buf, m, "", "[WPA3-SAE-CCMP][ESS]",
                       1793044702u, 6, -93, 421017660, -710195598);
        check("hidden network: empty quoted SSID", strcmp(buf, "1E:46:A7:6B:37:BA,\"\","
              "[WPA3-SAE-CCMP][ESS],2026-10-26 19:58:22,6,2437,-93,42.1017660,-71.0195598,0,30,,,WIFI\n") == 0);
    }
    printf("BLE row keeps the WigleWifi-1.6 column set:\n");
    {
        const uint8_t m[6] = {0x38, 0x4B, 0xC2, 0x01, 0x02, 0x03};
        wiglerow::ble(buf, sizeof buf, m, "Tile", 1793044702u, -71, 421017660, -710195598);
        check("BLE row", strcmp(buf, "38:4B:C2:01:02:03,\"Tile\",Misc [LE],2026-10-26 19:58:22,"
              "0,0,-71,42.1017660,-71.0195598,0,30,,,BLE\n") == 0);
    }
    printf("worst case fits applog's row buffer:\n");
    {
        const uint8_t m[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        char ssid[20]; memset(ssid, '"', 19); ssid[19] = 0;   // every char doubled
        char auth[64]; memset(auth, 'A', 63); auth[63] = 0;
        const int n = wiglerow::wifi(buf, sizeof buf, m, ssid, auth, 4102444799u, 165,
                                     -128, -899999999, -1799999999);
        check("formatted, not truncated", n > 0 && n < 256 && buf[n - 1] == '\n');
        printf("     (worst-case row %d bytes)\n", n);
        check("too small a buffer is refused, not truncated",
              wiglerow::wifi(buf, 40, m, ssid, auth, 4102444799u, 165, -128, 0, 0) == 0);
    }
    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
