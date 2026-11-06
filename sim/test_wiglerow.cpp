// Host test for the WiGLE row formatters (device/wiglerow.h), shared by the
// SD append path and the console WIGLE dump. Golden rows use synthetic values
// (made-up MACs, SSIDs, times and coordinates) in the exact WigleWifi-1.6 column
// layout the old whole-file exporter wrote, so the append logger must produce
// the same bytes for the same data.
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
        const uint8_t m[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
        // 2026-01-01 00:00:00 UTC
        const int n = wiglerow::wifi(buf, sizeof buf, m, "TESTNET-A", "[WPA2-PSK-CCMP][ESS]",
                                     1767225600u, 4, -88, 123456789, -987654321);
        check("TESTNET-A row", strcmp(buf, "02:00:00:00:00:01,\"TESTNET-A\",[WPA2-PSK-CCMP][ESS],"
              "2026-01-01 00:00:00,4,2427,-88,12.3456789,-98.7654321,0,30,,,WIFI\n") == 0);
        check("length returned", n == (int)strlen(buf));
    }
    {
        const uint8_t m[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x02};
        wiglerow::wifi(buf, sizeof buf, m, "TESTNET-5G", "[WPA2-PSK-CCMP][ESS]",
                       1767229261u, 36, -80, 123456800, -987654400);
        check("5 GHz row (channel 36 -> 5180)", strcmp(buf, "02:00:00:00:00:02,\"TESTNET-5G\","
              "[WPA2-PSK-CCMP][ESS],2026-01-01 01:01:01,36,5180,-80,12.3456800,-98.7654400,0,30,,,WIFI\n") == 0);
    }
    {
        const uint8_t m[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x03};
        wiglerow::wifi(buf, sizeof buf, m, "Test,Net,", "[OPEN][ESS]",
                       1767312045u, 6, -97, 123457000, -987655000);
        check("SSID with a comma stays quoted", strcmp(buf, "02:00:00:00:00:03,\"Test,Net,\","
              "[OPEN][ESS],2026-01-02 00:00:45,6,2437,-97,12.3457000,-98.7655000,0,30,,,WIFI\n") == 0);
    }
    {
        const uint8_t m[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x04};
        wiglerow::wifi(buf, sizeof buf, m, "", "[WPA3-SAE-CCMP][ESS]",
                       1767232801u, 6, -93, 123456790, -987654330);
        check("hidden network: empty quoted SSID", strcmp(buf, "02:00:00:00:00:04,\"\","
              "[WPA3-SAE-CCMP][ESS],2026-01-01 02:00:01,6,2437,-93,12.3456790,-98.7654330,0,30,,,WIFI\n") == 0);
    }
    printf("BLE row keeps the WigleWifi-1.6 column set:\n");
    {
        const uint8_t m[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x05};
        wiglerow::ble(buf, sizeof buf, m, "Tile", 1767232801u, -71, 123456790, -987654330);
        check("BLE row", strcmp(buf, "02:00:00:00:00:05,\"Tile\",Misc [LE],2026-01-01 02:00:01,"
              "0,0,-71,12.3456790,-98.7654330,0,30,,,BLE\n") == 0);
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
