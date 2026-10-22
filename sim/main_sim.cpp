// umbravox -- renders a screen from ui/ to a PNG, natively.
//
// The screen code here is the same translation unit the firmware will compile;
// only the State comes from somewhere else. That is the whole trick: layout and
// palette work happens in a sub-second loop instead of a flash cycle, and the
// art cannot drift from what the device draws because there is only one copy.
//
//   ./umbravox out.png                  one frame
//   ./umbravox out.png --frame 40       pick a point in the sweep
//   ./umbravox out.png --sheet 6        out-000.png .. out-005.png
//   ./umbravox out.png --scale 2        320x240, exactly what the panel shows
//   ./umbravox out.png --theme 2 --alert 1 --frame 20   an alert style mid-rite
//   ./umbravox out.png --pack path/to/folder   render a theme pack (composes
//                                       with --screen --sweep --alert --frame --scale)
//   ./umbravox --check-pack path/to/folder     print OK <name> or the reason;
//                                       exit 0 / 1; writes no PNG
#include "../ui/screens.h"
#include "../ui/demo.h"
#include "../ui/nav.h"
#include "../ui/palette.h"
#include "../ui/signatures.h"
#include "png_writer.h"
#include "packload.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

namespace {

void outName(char* out, size_t n, const char* base, int i) {
    const char* dot = strrchr(base, '.');
    const int stem = dot ? (int)(dot - base) : (int)strlen(base);
    snprintf(out, n, "%.*s-%03d%s", stem, base, i, dot ? dot : ".png");
}

} // namespace

int main(int argc, char** argv) {
    const char* out = (argc > 1 && argv[1][0] != '-') ? argv[1] : "umbravox.png";
    int frame = 0, scale = 4, sheet = 0;
    uint8_t themeId = pal::TH_UMBRAVOX;
    uint8_t sweepId = ui::SW_RADAR;
    uint8_t labelsId = 0;
    uint8_t netIdSim = 0;
    int alertSim = -1;                 // --alert: force an alert of this style
    const char* which = "scan";
    const char* packDir = nullptr;
    const char* checkDir = nullptr;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--frame") && i + 1 < argc) frame = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--scale") && i + 1 < argc) scale = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--sheet") && i + 1 < argc) sheet = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--screen") && i + 1 < argc) which = argv[++i];
        else if (!strcmp(argv[i], "--theme") && i + 1 < argc) themeId = (uint8_t)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--sweep") && i + 1 < argc) sweepId = (uint8_t)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--labels") && i + 1 < argc) labelsId = (uint8_t)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--netid") && i + 1 < argc) netIdSim = (uint8_t)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--alert") && i + 1 < argc) alertSim = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--pack") && i + 1 < argc) packDir = argv[++i];
        else if (!strcmp(argv[i], "--check-pack") && i + 1 < argc) checkDir = argv[++i];
    }

    // The loader's checks only: the verdict on stdout, nothing rendered.
    if (checkDir) {
        char why[128];
        const int id = packload::load(checkDir, why, sizeof why);
        if (id < 0) { printf("%s\n", why); return 1; }
        printf("OK %s\n", pal::themeName((uint8_t)id));
        return 0;
    }
    if (packDir) {
        char why[128];
        const int id = packload::load(packDir, why, sizeof why);
        if (id < 0) { fprintf(stderr, "umbravox: pack %s: %s\n", packDir, why); return 1; }
        themeId = (uint8_t)id;
    }

    // Nothing may draw before the palette points somewhere.
    pal::setTheme(themeId);

    static ui::Canvas c;
    const int n = sheet > 0 ? sheet : 1;

    for (int i = 0; i < n; i++) {
        const uint32_t f = sheet > 0 ? (uint32_t)(frame + i * 12) : (uint32_t)frame;
        ui::State s = ui::demoState(f);
        s.set.sweep = sweepId;
        s.set.theme = themeId;   // the screens branch on the setting, not the palette
        s.set.labels = labelsId; // 0 on-brand names, 1 plain
        s.netId = netIdSim;      // preview the MESH screen's enrolled state
        if (alertSim >= 0) {
            // Hold an alert up so each style can be rendered at any point in its
            // rite: --frame is the alert's age in 20 ms steps.
            s.set.alertStyle = (uint8_t)alertSim;
            s.alertActive = true;
            s.alertThreat = ui::TH_DRONE;
            s.alertConf   = ui::CONF_HIGH;
            strcpy(s.alertWhy,  "DRONEID");
            strcpy(s.alertName, "60:60:1F:A2:4C:19");
            s.alertAge = (uint16_t)(f * 20);
        }
        ui::clampSettings(s.set); // the device cannot reach an impossible pairing; neither may the sim
        const bool codex  = !strcmp(which, "codex");
        const bool attune = !strcmp(which, "attune");
        const bool detail = !strcmp(which, "detail");
        const bool meshs  = !strcmp(which, "mesh");
        const bool domain = !strcmp(which, "domain");
        const bool omens  = !strcmp(which, "omens");
        const bool echoes = !strcmp(which, "echoes");
        const bool grim   = !strcmp(which, "grimoire");
        const bool aspect = !strcmp(which, "aspect");
        s.screen = aspect ? ui::SCR_ASPECT : attune ? ui::SCR_ATTUNE : detail ? ui::SCR_DETAIL
                 : echoes ? ui::SCR_ECHOES : grim ? ui::SCR_GRIMOIRE
                 : omens ? ui::SCR_OMENS : domain ? ui::SCR_DOMAIN
                 : meshs ? ui::SCR_MESH : codex ? ui::SCR_CODEX : ui::SCR_SCAN;
        s.filter = ui::F_WIFI;
        if (aspect)      ui::drawAspect(c, s);
        else if (attune) ui::drawAttune(c, s);
        else if (meshs)  ui::drawMesh(c, s);
        else if (domain) ui::drawDomain(c, s);
        else if (omens)  ui::drawOmens(c, s);
        else if (echoes) ui::drawEchoes(c, s);
        else if (grim)   ui::drawGrimoire(c, s);
        else if (detail) ui::drawDetail(c, s);
        else if (codex)  ui::drawCodex(c, s);
        else             ui::drawScan(c, s);

        char path[512];
        if (sheet > 0) outName(path, sizeof(path), out, i);
        else snprintf(path, sizeof(path), "%s", out);

        if (!png::write(path, c.px, ui::W, ui::H, scale)) {
            fprintf(stderr, "umbravox: cannot write %s\n", path);
            return 1;
        }
        printf("%s  %dx%d  frame %u\n", path, ui::W * scale, ui::H * scale, f);
    }
    return 0;
}
