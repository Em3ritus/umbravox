// Host tests for beacon security parsing.
//
// Built from real element bytes rather than mocks: every length in an RSN
// element comes off the air and is attacker-controlled, so the truncation
// cases matter as much as the well-formed ones.
#include "../ui/wifi_auth.h"
#include <stdio.h>
#include <string.h>
#include <vector>

static int fails = 0;
static void eq(const char* what, const char* got, const char* want) {
    if (strcmp(got, want)) { printf("  FAIL %-26s got \"%s\" want \"%s\"\n", what, got, want); fails++; }
    else                   { printf("  ok   %-26s %s\n", what, got); }
}
static void check(const char* what, bool ok) {
    if (!ok) { printf("  FAIL %s\n", what); fails++; }
    else     { printf("  ok   %s\n", what); }
}

// A beacon body: 8 timestamp, 2 interval, 2 capability, then elements.
static std::vector<uint8_t> body(uint16_t cap, std::vector<uint8_t> tags) {
    std::vector<uint8_t> b(8, 0);
    b.push_back(0x64); b.push_back(0x00);          // interval
    b.push_back(cap & 0xFF); b.push_back(cap >> 8);
    for (uint8_t v : tags) b.push_back(v);
    return b;
}

// RSN element: version 1, group CCMP, one pairwise CCMP, one AKM of `akm`.
static std::vector<uint8_t> rsn(uint8_t akm, uint8_t pairwise = 4) {
    return {48, 20,
            0x01, 0x00,
            0x00,0x0F,0xAC, 0x04,
            0x01,0x00, 0x00,0x0F,0xAC, pairwise,
            0x01,0x00, 0x00,0x0F,0xAC, akm,
            0x00,0x00};
}

int main() {
    using namespace ui;
    char s[64];

    printf("well formed:\n");
    auto b = body(0x0431, rsn(2));                 // ESS + Privacy, AKM PSK
    uint16_t a = parseAuth(b.data(), (int)b.size());
    authString(a, s, sizeof s);
    eq("WPA2 PSK CCMP", s, "[WPA2-PSK-CCMP][ESS]");
    check("short form WPA2", !strcmp(authShort(a), "WPA2"));

    b = body(0x0431, rsn(8));                      // AKM SAE
    a = parseAuth(b.data(), (int)b.size());
    authString(a, s, sizeof s);
    eq("WPA3 SAE", s, "[WPA3-SAE-CCMP][ESS]");
    check("short form WPA3", !strcmp(authShort(a), "WPA3"));

    b = body(0x0431, rsn(1));                      // AKM 802.1X
    a = parseAuth(b.data(), (int)b.size());
    authString(a, s, sizeof s);
    eq("WPA2 enterprise", s, "[WPA2-EAP-CCMP][ESS]");

    b = body(0x0431, rsn(2, 2));                   // pairwise TKIP
    a = parseAuth(b.data(), (int)b.size());
    authString(a, s, sizeof s);
    eq("WPA2 PSK TKIP", s, "[WPA2-PSK-TKIP][ESS]");

    printf("no RSN element:\n");
    b = body(0x0001, {});                          // ESS, no privacy
    a = parseAuth(b.data(), (int)b.size());
    authString(a, s, sizeof s);
    eq("open network", s, "[OPEN][ESS]");
    check("short form OPEN", !strcmp(authShort(a), "OPEN"));

    b = body(0x0011, {});                          // ESS + privacy, nothing else
    a = parseAuth(b.data(), (int)b.size());
    authString(a, s, sizeof s);
    eq("privacy alone is WEP", s, "[WEP][ESS]");

    b = body(0x0002, {});                          // IBSS
    a = parseAuth(b.data(), (int)b.size());
    authString(a, s, sizeof s);
    eq("ad-hoc", s, "[OPEN][IBSS]");

    printf("vendor WPA1:\n");
    std::vector<uint8_t> wpa = {221, 22, 0x00,0x50,0xF2, 0x01,
                                0x01,0x00,
                                0x00,0x50,0xF2, 0x02,
                                0x01,0x00, 0x00,0x50,0xF2, 0x02,
                                0x01,0x00, 0x00,0x50,0xF2, 0x02};
    b = body(0x0011, wpa);
    a = parseAuth(b.data(), (int)b.size());
    authString(a, s, sizeof s);
    eq("WPA1 PSK TKIP", s, "[WPA-PSK-TKIP][ESS]");
    check("WPA1 is not WEP", !(a & AUTH_WEP));

    printf("malformed input must not read past the end:\n");
    std::vector<uint8_t> bad = {48, 20, 0x01, 0x00};      // claims 20, gives 2
    b = body(0x0431, bad);
    a = parseAuth(b.data(), (int)b.size());
    check("truncated element survives", true);            // no crash is the test

    std::vector<uint8_t> liar = {48, 18, 0x01,0x00, 0x00,0x0F,0xAC,0x04,
                                 0xFF,0xFF,                 // 65535 pairwise
                                 0x00,0x0F,0xAC,0x04,
                                 0x01,0x00, 0x00,0x0F,0xAC, 0x02};
    b = body(0x0431, liar);
    a = parseAuth(b.data(), (int)b.size());
    check("absurd suite count survives", true);

    a = parseAuth(nullptr, 0);
    check("null body is safe", a == 0);
    b = body(0x0431, {});
    a = parseAuth(b.data(), 4);                           // shorter than fixed params
    check("short body is safe", a == 0);

    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
