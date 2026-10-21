#include "muffinseal.h"
#include "muffin.h"
#include "motif.h"

namespace ui {
void drawMuffin(Canvas& c, int cx, int cy, int r, uint32_t frame, int liftOverride) {
    drawEmblem(c, muffinEmblem(), cx, cy, r, frame, liftOverride);
}

static_assert(MUFFIN_W <= themepack::MAX_EMBLEM_DIM && MUFFIN_H <= themepack::MAX_EMBLEM_DIM,
              "the baked muffin must fit an Emblem");
static_assert(MUFFIN_NCOL <= themepack::MAX_EMBLEM_COLORS, "the baked muffin must fit an Emblem");

const themepack::Emblem& muffinEmblem() {
    // Built on first use (a function-local static), so there is no static-
    // initialisation order to get wrong.
    static const themepack::Emblem e = [] {
        themepack::Emblem m{};
        m.w = (uint8_t)MUFFIN_W;
        m.h = (uint8_t)MUFFIN_H;
        m.ncol = (uint8_t)MUFFIN_NCOL;
        for (int i = 0; i < MUFFIN_NCOL; i++) m.pal[i] = MUFFIN_PAL[i];
        m.px = MUFFIN_PX;
        return m;
    }();
    return e;
}

} // namespace ui
