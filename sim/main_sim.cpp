// aethersim -- renders a screen from ui/ to a PNG, natively.
//
// The screen code here is the same translation unit the firmware will compile;
// only the State comes from somewhere else. That is the whole trick: layout and
// palette work happens in a sub-second loop instead of a flash cycle, and the
// art cannot drift from what the device draws because there is only one copy.
//
//   ./aethersim out.png                  one frame
//   ./aethersim out.png --frame 40       pick a point in the sweep
//   ./aethersim out.png --sheet 6        out-000.png .. out-005.png
//   ./aethersim out.png --scale 2        320x240, exactly what the panel shows
#include "../ui/screens.h"
#include "../ui/demo.h"
#include "../ui/nav.h"
#include "../ui/palette.h"
#include "png_writer.h"
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
    const char* out = (argc > 1 && argv[1][0] != '-') ? argv[1] : "aetherscan.png";
    int frame = 0, scale = 4, sheet = 0;
    uint8_t themeId = pal::TH_AETHERSCAN;
    const char* which = "scan";

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--frame") && i + 1 < argc) frame = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--scale") && i + 1 < argc) scale = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--sheet") && i + 1 < argc) sheet = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--screen") && i + 1 < argc) which = argv[++i];
        else if (!strcmp(argv[i], "--theme") && i + 1 < argc) themeId = (uint8_t)atoi(argv[++i]);
    }

    // Nothing may draw before the palette points somewhere.
    pal::setTheme(themeId);

    static ui::Canvas c;
    const int n = sheet > 0 ? sheet : 1;

    for (int i = 0; i < n; i++) {
        const uint32_t f = sheet > 0 ? (uint32_t)(frame + i * 12) : (uint32_t)frame;
        ui::State s = ui::demoState(f);
        const bool codex  = !strcmp(which, "codex");
        const bool attune = !strcmp(which, "attune");
        const bool detail = !strcmp(which, "detail");
        const bool meshs  = !strcmp(which, "mesh");
        const bool domain = !strcmp(which, "domain");
        const bool omens  = !strcmp(which, "omens");
        const bool echoes = !strcmp(which, "echoes");
        const bool grim   = !strcmp(which, "grimoire");
        s.screen = attune ? ui::SCR_ATTUNE : detail ? ui::SCR_DETAIL
                 : echoes ? ui::SCR_ECHOES : grim ? ui::SCR_GRIMOIRE
                 : omens ? ui::SCR_OMENS : domain ? ui::SCR_DOMAIN
                 : meshs ? ui::SCR_MESH : codex ? ui::SCR_CODEX : ui::SCR_SCAN;
        s.filter = ui::F_WIFI;
        if (attune)      ui::drawAttune(c, s);
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
            fprintf(stderr, "aethersim: cannot write %s\n", path);
            return 1;
        }
        printf("%s  %dx%d  frame %u\n", path, ui::W * scale, ui::H * scale, f);
    }
    return 0;
}
