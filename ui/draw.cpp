#include "draw.h"
#include "portrait.h"
#include "portrait_layout.h"
#include "framediff.h"
#include "prevstate.h"
#include "nav.h"

namespace ui {

void sizeCanvas(Canvas& c, uint8_t orient) {
    if (orient == ORIENT_PORTRAIT) c.setSize(pl::PW, pl::PH);
    else                           c.setSize(W, H);
}

void drawScreen(Canvas& c, const State& s, Dirty* d) {
    if (s.set.orient == ORIENT_PORTRAIT) {
        sizeCanvas(c, ORIENT_PORTRAIT);
        drawPortrait(c, s);
        if (d) frameDiff(c, *d);
        return;
    }
    // Landscape: exactly the screens as they always were.
    sizeCanvas(c, ORIENT_LANDSCAPE);
    switch (s.screen) {
        case SCR_CODEX:    drawCodex(c, s, d);    break;
        case SCR_DETAIL:   drawDetail(c, s, d);   break;
        case SCR_ECHOES:   drawEchoes(c, s, d);   break;
        case SCR_GRIMOIRE: drawGrimoire(c, s, d); break;
        case SCR_OMENS:    drawOmens(c, s, d);    break;
        case SCR_DOMAIN:   drawDomain(c, s, d);   break;
        case SCR_MESH:     drawMesh(c, s, d);     break;
        case SCR_ATTUNE:   drawAttune(c, s, d);   break;
        case SCR_ASPECT:   drawAspect(c, s, d);   break;
        case SCR_FILES:    drawFiles(c, s, d);    break;
        default:           drawScan(c, s, d);     break;
    }
}

void orientationChanged() {
    resetPrevStates();
    frameDiffReset();
}

} // namespace ui
