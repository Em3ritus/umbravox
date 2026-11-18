// Host test: the alert repeat table. Fixed things alert once and then stay quiet for ALERT REPEAT; a tracker that
// travels with you re-alerts every 10 min and a drone every 20 min; a confidence rise always shows.
// Build: make -C sim test_alert_repeat
#include "../ui/alert_repeat.h"
#include <cstdio>
#include <cstring>
#include <string>
static int fails = 0;
static void expect(bool c, const char* m) { if (!c) { ++fails; printf("  FAIL %s\n", m); } else printf("  ok   %s\n", m); }

using namespace alertrepeat;
static ui::AlertRec rec(uint8_t threat, uint8_t conf, int macLast, const char* name) {
    ui::AlertRec a;
    a.threat = threat; a.conf = conf;
    std::snprintf(a.name, sizeof a.name, "%s", name);
    if (macLast >= 0) { a.address = ui::AlertAddress::BlePublic; const uint8_t m[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, (uint8_t)macLast}; std::memcpy(a.mac, m, 6); }
    return a;
}
static const uint32_t MIN = 60000;

int main() {
    const auto cam = rec(ui::TH_CAMERA, ui::CONF_MED, 1, "CAM-01");
    {   Table t; const Verdict v = t.see(cam, 0, 1);
        expect(v.show && !v.repeat, "first_sighting_shows"); }
    {   Table t; const Verdict a = t.see(cam, 0, 1), b = t.see(cam, 2000, 1), c = t.see(cam, 5000, 1);
        expect(a.show && b.show && c.show && a.episode == b.episode && b.episode == c.episode, "same_episode_keeps_showing"); }
    {   Table t; t.see(cam, 0, 1);
        expect(!t.see(cam, 7000, 1).show, "fixed device quiet after a gap inside the 10 min cooldown");
        expect(t.see(cam, 10 * MIN + 7000, 1).show, "fixed device back after the cooldown shows");
        Table u; u.see(cam, 0, 0);
        expect(!u.see(cam, 30000, 0).show, "1 min setting: quiet at 30 s");
        expect(u.see(cam, MIN + 7000, 0).show, "1 min setting: back after a minute"); }
    {   Table t; const auto low = rec(ui::TH_CAMERA, ui::CONF_LOW, 1, "CAM-01");
        t.see(low, 0, 3);
        expect(!t.see(low, 600 * MIN, 3).show, "until_reboot_never_again");
        expect(t.see(rec(ui::TH_CAMERA, ui::CONF_MED, 1, "CAM-01"), 600 * MIN + 7000, 3).show, "until reboot: a confidence rise still shows"); }
    {   Table t; const auto hi = rec(ui::TH_CAMERA, ui::CONF_HIGH, 1, "CAM-01");
        const Verdict a = t.see(cam, 0, 1);
        const Verdict b = t.see(hi, 8000, 1);
        expect(b.show && b.episode != a.episode, "conf_rise_breaks_through");
        expect(!t.see(hi, 16000, 1).show, "the same higher confidence again is quiet"); }
    {   Table t; const auto f = rec(ui::TH_FOLLOW, ui::CONF_HIGH, 2, "DD:EE:02 0.5KM");
        expect(t.see(f, 0, 1).show, "follow: first shows");
        const Verdict v = t.see(f, 10 * MIN, 1);
        expect(v.show && v.repeat, "follow_every_10_min"); }
    {   Table t; const auto f = rec(ui::TH_FOLLOW, ui::CONF_HIGH, 2, "DD:EE:02 0.5KM");
        t.see(f, 0, 3);
        expect(t.see(f, 10 * MIN, 3).show, "follow_ignores_setting"); }
    {   Table t; const auto f = rec(ui::TH_FOLLOW, ui::CONF_HIGH, 2, "DD:EE:02 0.5KM");
        t.see(f, 1000, 1); t.dismissCurrent(4000);                    // tapped 3 s into the follow alert
        expect(t.see(f, 10 * MIN, 1).show, "follow: a dismiss does not cost the next 10-min re-alert (blescan paces it)"); }
    {   Table t; const auto f = rec(ui::TH_FOLLOW, ui::CONF_HIGH, 2, "DD:EE:02 0.5KM");
        t.see(f, 2000, 1);                                            // the loop saw the first raise 2 s late
        expect(t.see(f, 10 * MIN + 500, 1).show, "follow: a late first sighting does not delay the next re-alert"); }
    {   Table t; const auto d = rec(ui::TH_DRONE, ui::CONF_HIGH, 3, "DRONE-1");
        const Verdict first = t.see(d, 0, 1);
        bool allShown = true; uint32_t now = 3000; Verdict v{};
        for (; now < 20 * MIN; now += 3000) { v = t.see(d, now, 1); if (!v.show || v.episode != first.episode) allShown = false; }
        expect(allShown, "drone: the first episode keeps showing while it stays");
        v = t.see(d, now, 1);
        expect(v.show && v.repeat && v.episode != first.episode && v.firstMs == 0, "drone_present_realerts_at_20min"); }
    {   Table t; const auto d = rec(ui::TH_DRONE, ui::CONF_HIGH, 3, "DRONE-1");
        t.see(d, 0, 1);
        const Verdict v = t.see(d, 50 * MIN, 1);
        expect(v.show && !v.repeat && v.firstMs == 50 * MIN, "drone_back_after_long_absence_is_fresh"); }
    {   Table t; t.see(cam, 0, 1); t.dismissCurrent(3000);
        expect(!t.see(cam, 4000, 1).show, "dismiss_starts_cooldown: hidden for the rest of the episode");
        expect(!t.see(cam, 3000 + 10 * MIN - 1000, 1).show, "dismiss: still quiet just before the cooldown from the dismiss");
        expect(t.see(cam, 3000 + 10 * MIN + 7000, 1).show, "dismiss: back after the cooldown"); }
    {   Table t; t.see(rec(ui::TH_CAMERA, ui::CONF_MED, 2, "AA 0.5KM"), 0, 1);
        expect(!t.see(rec(ui::TH_CAMERA, ui::CONF_MED, 2, "AA 1.2KM"), 7000, 1).show, "key_is_address_not_name"); }
    {   Table t; t.see(rec(ui::TH_CAMERA, ui::CONF_MED, -1, "RING-A"), 0, 1);
        expect(t.see(rec(ui::TH_CAMERA, ui::CONF_MED, -1, "RING-B"), 7000, 1).show, "name_key_without_address");
        expect(!t.see(rec(ui::TH_CAMERA, ui::CONF_MED, -1, "RING-A"), 14000, 1).show, "name key: the first is still quiet"); }
    {   Table t; t.see(cam, 0, 1);
        expect(t.see(rec(ui::TH_CAMERA, ui::CONF_MED, 9, "CAM-09"), 7000, 1).show, "two_devices_independent"); }
    {   Table t;
        for (int i = 0; i < SLOTS + 1; i++) t.see(rec(ui::TH_CAMERA, ui::CONF_MED, 100 + i, "C"), (uint32_t)i * 10000, 1);
        const Verdict v = t.see(rec(ui::TH_CAMERA, ui::CONF_MED, 100, "C"), 200000, 1);
        expect(v.show && !v.repeat, "evicts_oldest"); }
    {   Table t; const uint32_t t0 = 0xFFFFFFFFu - 1000;
        t.see(cam, t0, 1);
        expect(!t.see(cam, t0 + 7000, 1).show, "wrap: quiet just after");
        expect(t.see(cam, t0 + 10 * MIN + 7000, 1).show, "cooldown_survives_wrap"); }
    {   char b[20];
        reason(b, sizeof b, ui::TH_DRONE, 40);  expect(std::string(b) == "STILL HERE 40 MIN", "reason_text drone");
        reason(b, sizeof b, ui::TH_FOLLOW, 50); expect(std::string(b) == "WITH YOU 50 MIN", "reason_text follow");
        reason(b, sizeof b, ui::TH_DRONE, 5000); expect(std::string(b) == "STILL HERE 999 MIN", "reason_text clamps to 999");
        char s[8]; reason(s, sizeof s, ui::TH_DRONE, 40); expect(std::strlen(s) == 7, "reason_text stays within n"); }
    {   expect(!followDue(0, 10 * MIN - 1), "follow_due: not before 10 min");
        expect(followDue(0, 10 * MIN), "follow_due: at 10 min");
        expect(followDue(0xFFFFFFFFu - 1000, 10 * MIN), "follow_due: across the wrap"); }
    {   expect(repeatMs(0) == MIN && repeatMs(1) == 10 * MIN && repeatMs(2) == 60 * MIN && repeatMs(3) == UINT32_MAX && repeatMs(9) == 10 * MIN, "repeatMs choices"); }
    {   // A follow alert is never masked by a newer alert from the other radio (it lasts only 6 s).
        ui::AlertRec c[2] = {rec(ui::TH_CAMERA, ui::CONF_HIGH, 5, "CAM"), rec(ui::TH_FOLLOW, ui::CONF_HIGH, 6, "AA 1KM")};
        prioritise(c, 2);
        expect(c[0].threat == ui::TH_FOLLOW && c[1].threat == ui::TH_CAMERA, "prioritise: follow goes first");
        ui::AlertRec d[2] = {rec(ui::TH_CAMERA, ui::CONF_MED, 5, "CAM"), rec(ui::TH_DRONE, ui::CONF_HIGH, 6, "D")};
        prioritise(d, 2);
        expect(d[0].threat == ui::TH_CAMERA, "prioritise: otherwise newest first, unchanged"); }
    {   // blescan's single alert slot: a follow alert holds it for its 6 s against other BLE hits.
        ui::AlertRec held = rec(ui::TH_FOLLOW, ui::CONF_HIGH, 6, "AA 1KM"); held.atMs = 100000;
        expect(!replaces(held, ui::TH_TRACKER, ui::CONF_HIGH, 101000), "replaces: the tracker's own advert does not overwrite its follow alert");
        expect(replaces(held, ui::TH_TRACKER, ui::CONF_HIGH, 106001), "replaces: after 6 s it may");
        expect(replaces(held, ui::TH_FOLLOW, ui::CONF_HIGH, 101000), "replaces: another follow alert may");
        ui::AlertRec cam = rec(ui::TH_CAMERA, ui::CONF_MED, 7, "C"); cam.atMs = 100000;
        expect(replaces(cam, ui::TH_TRACKER, ui::CONF_HIGH, 101000), "replaces: higher or equal confidence overwrites (as before)");
        expect(!replaces(cam, ui::TH_TRACKER, ui::CONF_LOW, 101000), "replaces: lower confidence waits 6 s (as before)");
        expect(replaces(cam, ui::TH_TRACKER, ui::CONF_LOW, 106001), "replaces: ...then overwrites");
        expect(!replaces(cam, ui::TH_NONE, ui::CONF_HIGH, 200000), "replaces: no threat, no write"); }
    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails ? 1 : 0;
}
