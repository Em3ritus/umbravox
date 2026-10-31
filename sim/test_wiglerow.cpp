// Host test for the WiGLE row formatters (device/wiglerow.h), shared by the
// SD append path and the console WIGLE dump. The golden rows below are
// synthetic fixtures: made-up MACs, SSIDs and coordinates chosen to exercise
// the format's edge cases (2.4/5 GHz, comma-in-SSID quoting, hidden SSID, BLE,
// worst-case buffer), so the append logger must produce the same bytes.
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
    printf("WiFi rows match the golden fixtures byte for byte:\n");
    {
        const uint8_t m[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
        // 2026-10-23 18:37:21 UTC
        const int n = wiglerow::wifi(buf, sizeof buf, m, "TESTNET-2G", "[WPA2-PSK-CCMP][ESS]",
                                     1792780641u, 4, -88, 111111100, -222222200);
        check("WPA2 2.4 GHz row", strcmp(buf, "02:00:00:00:00:01,\"TESTNET-2G\",[WPA2-PSK-CCMP][ESS],"
              "2026-10-23 18:37:21,4,2427,-88,11.1111100,-22.2222200,0,30,,,WIFI\n") == 0);
        check("length returned", n == (int)strlen(buf));
    }
    {
        const uint8_t m[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x02};
        wiglerow::wifi(buf, sizeof buf, m, "TESTNET-5G", "[WPA2-PSK-CCMP][ESS]",
                       1793044703u, 36, -80, 333333300, -444444400);
        check("5 GHz row (channel 36 -> 5180)", strcmp(buf, "02:00:00:00:00:02,\"TESTNET-5G\","
              "[WPA2-PSK-CCMP][ESS],2026-10-26 19:58:23,36,5180,-80,33.3333300,-44.4444400,0,30,,,WIFI\n") == 0);
    }
    {
        const uint8_t m[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x03};
        wiglerow::wifi(buf, sizeof buf, m, "TEST,NET", "[OPEN][ESS]",
                       1793045046u, 6, -97, 123456700, -765432100);
        check("SSID with a comma stays quoted", strcmp(buf, "02:00:00:00:00:03,\"TEST,NET\","
              "[OPEN][ESS],2026-10-26 20:04:06,6,2437,-97,12.3456700,-76.5432100,0,30,,,WIFI\n") == 0);
    }
    {
        const uint8_t m[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x04};
        wiglerow::wifi(buf, sizeof buf, m, "", "[WPA3-SAE-CCMP][ESS]",
                       1793044702u, 6, -93, 100000000, -100000000);
        check("hidden network: empty quoted SSID", strcmp(buf, "02:00:00:00:00:04,\"\","
              "[WPA3-SAE-CCMP][ESS],2026-10-26 19:58:22,6,2437,-93,10.0000000,-10.0000000,0,30,,,WIFI\n") == 0);
    }
    printf("BLE row keeps the WigleWifi-1.6 column set:\n");
    {
        const uint8_t m[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x05};
        wiglerow::ble(buf, sizeof buf, m, "TESTTAG", 1793044702u, -71, 100000000, -100000000);
        check("BLE row", strcmp(buf, "02:00:00:00:00:05,\"TESTTAG\",Misc [LE],2026-10-26 19:58:22,"
              "0,0,-71,10.0000000,-10.0000000,0,30,,,BLE\n") == 0);
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
