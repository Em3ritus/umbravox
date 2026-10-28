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
//   ./umbravox out.png --nodes 3        mesh nodes connected (scan footer, MESH)
//   ./umbravox out.png --screen attune --setpage 2 --autolog 1   settings page 2
//   ./umbravox out.png --screen files --files 64 --scroll 59 --selected 61 --active 62 --confirm
//                                       FILES: N logs (default 5), scroll, selection,
//                                       REC row, the delete card; --nocard, --result 1|2,
//                                       --partial (walk bounded)
//   ./umbravox --check-pack path/to/folder     print OK <name> or the reason;
//                                       exit 0 / 1; writes no PNG
#include "../ui/screens.h"
#include "../ui/demo.h"
#include "../ui/nav.h"
#include "../ui/files.h"
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
    uint8_t idleModeSim = 0;           // --idlemode 0 DIM, 1 OFF, 2 ANIM
    int alertSim = -1;                 // --alert: force an alert of this style
    int nodesSim = -1;                 // --nodes N: mesh nodes connected (default: the demo's)
    int setPageSim = 1;                // --setpage N: settings page, 1-based
    uint8_t autoLogSim = 0;            // --autolog 0 OFF, 1 ON
    int filesSim = 5, fileSelSim = -1, fileActSim = -1, fileScrollSim = 0, fileResultSim = 0;
    bool fileConfirmSim = false, noCardSim = false, partialSim = false;
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
        else if (!strcmp(argv[i], "--idlemode") && i + 1 < argc) idleModeSim = (uint8_t)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--alert") && i + 1 < argc) alertSim = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--nodes") && i + 1 < argc) nodesSim = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--setpage") && i + 1 < argc) setPageSim = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--autolog") && i + 1 < argc) autoLogSim = (uint8_t)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--files") && i + 1 < argc) filesSim = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--selected") && i + 1 < argc) fileSelSim = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--active") && i + 1 < argc) fileActSim = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--scroll") && i + 1 < argc) fileScrollSim = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--result") && i + 1 < argc) fileResultSim = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--confirm")) fileConfirmSim = true;
        else if (!strcmp(argv[i], "--nocard")) noCardSim = true;
        else if (!strcmp(argv[i], "--partial")) partialSim = true;
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
        s.set.idleMode = idleModeSim;
        s.netId = netIdSim;      // preview the MESH screen's enrolled state
        if (nodesSim >= 0) s.nNodes = (uint8_t)(nodesSim > 255 ? 255 : nodesSim);
        s.set.autoLog = autoLogSim;
        s.setPage = (uint8_t)(setPageSim >= 1 && setPageSim <= ui::SET_PAGES ? setPageSim - 1 : 0);
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
        const bool files  = !strcmp(which, "files");
        if (files) {
            // Invented logs, newest first: dated ones, then two dateless.
            static ui::FileList L;
            L = ui::FileList();
            L.card = !noCardSim;
            const int k = noCardSim ? 0 : (filesSim < 0 ? 0 : filesSim);
            for (int r = 0; r < k && r < ui::MAX_FILE_ROWS; r++) {
                if (r >= 2 && r == k - 1) snprintf(L.rows[r].name, ui::FILE_NAME_MAX, "WD-%04d.csv", 3);
                else snprintf(L.rows[r].name, ui::FILE_NAME_MAX, "WD-2026%02d%02d-%02d%02d%02d.csv",
                              10 - r / 27, 27 - r % 27, 21 - r % 12, (r * 17) % 60, (r * 7) % 60);
                L.rows[r].size = (uint32_t)(((r * 2654435761u) >> 8) % 9000000u) + 41000u;
            }
            L.n = (uint8_t)(k < ui::MAX_FILE_ROWS ? k : ui::MAX_FILE_ROWS);
            L.found = (uint16_t)k;
            if (L.card) {
                L.cardMB = 28899;                    // the CYD test card
                for (int r = 0; r < L.n; r++) L.logKB += (L.rows[r].size + 1023) / 1024;
            }
            L.partial = partialSim && L.card;
            ui::Nav nv;
            nv.screen = ui::SCR_FILES;
            ui::filesRescanned(nv, L, nullptr);
            nv.scroll = (int16_t)fileScrollSim;
            nv.fileSel = (int8_t)(fileSelSim < L.n ? fileSelSim : -1);
            ui::filesSetActive(nv, fileActSim);
            nv.fileConfirm = fileConfirmSim;
            nv.fileResult = (uint8_t)fileResultSim;
            ui::filesPublish(s, nv, L);
        }
        s.screen = files ? ui::SCR_FILES : aspect ? ui::SCR_ASPECT : attune ? ui::SCR_ATTUNE : detail ? ui::SCR_DETAIL
                 : echoes ? ui::SCR_ECHOES : grim ? ui::SCR_GRIMOIRE
                 : omens ? ui::SCR_OMENS : domain ? ui::SCR_DOMAIN
                 : meshs ? ui::SCR_MESH : codex ? ui::SCR_CODEX : ui::SCR_SCAN;
        s.filter = ui::F_WIFI;
        if (files)       ui::drawFiles(c, s);
        else if (aspect) ui::drawAspect(c, s);
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
