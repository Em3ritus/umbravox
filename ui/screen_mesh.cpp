// MESH -- who is listening, and to what.
//
// Without this the mesh is invisible from the device: a node could be flat,
// out of range, or never assigned, and the only symptom would be a capture
// rate that felt a bit low. The marks (age) and the loss figures matter most.
#include "screens.h"
#include "palette.h"
#include "font5x7.h"
#include "layout.h"

namespace ui {
namespace {

// A header line for the Core and the mesh-wide loss, then the nodes in two
// columns of eight so sixteen fit at a pitch that still reads. The old
// five-column table topped out at four nodes.
constexpr int HEAD_Y = 15, RULE_Y = 24;
constexpr int ROW_Y = 26, ROW_HT = 8, COL_ROWS = 8;
constexpr int COL_X[2] = {5, 82};     // left edge of each node column
constexpr int COL_W = 73;
constexpr int SUM_Y = 92;             // the totals line

void u2s(char* out, uint32_t v) {
    char tmp[12];
    int n = 0;
    do { tmp[n++] = (char)('0' + v % 10); v /= 10; } while (v && n < 11);
    int i = 0;
    while (n) out[i++] = tmp[--n];
    out[i] = 0;
}
void cat(char* o, const char* s) { int i = 0; while (o[i]) i++; for (int k = 0; s[k]; k++) o[i++] = s[k]; o[i] = 0; }

void idStr(char* out, const uint8_t* id) {
    static const char* H = "0123456789ABCDEF";
    int k = 0;
    for (int i = 1; i < 3; i++) {
        out[k++] = H[(id[i] >> 4) & 0xF];
        out[k++] = H[id[i] & 0xF];
        if (i < 2) out[k++] = ':';
    }
    out[k] = 0;
}

// One node: age mark, last two octets, loss. Dim ink when it is not live.
void cell(Canvas& c, int x, int y, uint16_t mark, const char* id, uint16_t idCol,
          const char* loss, uint16_t lossCol) {
    c.fill(x, y + 1, 3, 5, mark);
    c.text(x + 6, y, id, idCol);
    c.textRight(x + COL_W - 1, y, loss, lossCol);
}

// Under 5% is the air being the air; a fifth missing is a problem.
uint16_t lossInk(int pct) {
    return pct >= 20 ? pal::RED : pct >= 5 ? pal::AMBER : pal::GREEN;
}

} // namespace

void drawMesh(Canvas& c, const State& s, Dirty* d) {
    c.clear(pal::VOID);
    drawHeader(c, s, "MESH");

    c.fill(1, BODY_Y, 158, BODY_H, pal::PANEL);
    c.rect(1, BODY_Y, 158, BODY_H, pal::EDGE);

    char cover[12], id[12];

    // The Core's own entry first. It is a listener too, and when it is camping
    // it is the only thing covering the report channel. It has no link to
    // lose anything over, so it carries no loss figure.
    cover[0] = 0;
    cat(cover, "CORE ");
    if (s.coreCamping) { cat(cover, "CH"); char n[8]; u2s(n, s.coreChan); cat(cover, n); }
    else               cat(cover, "SWEEP");
    c.fill(5, HEAD_Y + 1, 3, 5, pal::WITCH_HI);
    c.text(11, HEAD_Y, cover, pal::BRIGHT);

    // The figure the operator acts on: the whole mesh's report loss. High and
    // the channel is saturated; another coordinator splits it.
    {
        char b[16]; b[0] = 0; char n[12];
        cat(b, "LOSS "); u2s(n, s.meshLossPct); cat(b, n); cat(b, "%");
        c.textRight(155, HEAD_Y, b, lossInk(s.meshLossPct));
    }
    c.hline(5, RULE_Y, 148, pal::EDGE);
    c.vline(COL_X[1] - 4, ROW_Y, COL_ROWS * ROW_HT, Canvas::mix(pal::PANEL, pal::EDGE, 1, 2));

    if (s.nNodes == 0) {
        c.textCenter(W / 2, ROW_Y + 22, "NO NODES", pal::DIM);
        c.textCenter(W / 2, ROW_Y + 32, "CORE IS SWEEPING ALONE", pal::EDGE_LIT);
    }

    for (int i = 0; i < s.nNodes && i < MAX_NODE_ROWS && i < 2 * COL_ROWS; i++) {
        const NodeRow& nd = s.nodes[i];
        const int x = COL_X[i / COL_ROWS];
        const int y = ROW_Y + (i % COL_ROWS) * ROW_HT;

        // Green while it is reporting, amber once it has gone quiet long
        // enough to be worth looking at, red when it is effectively gone.
        const uint16_t mark = nd.ageS < 10 ? pal::GREEN
                            : nd.ageS < 20 ? pal::AMBER : pal::RED;

        // Last two octets: the tail is still enough to tell nodes apart.
        idStr(id, nd.id);
        char ls[12];
        u2s(ls, nd.lossPct); cat(ls, "%");
        cell(c, x, y, mark, id, nd.live ? pal::TEXT : pal::DIM, ls, lossInk(nd.lossPct));
    }

    // HITS counts sightings, and an AP beaconing ten times a second racks
    // them up fast -- the number people actually want is how many distinct
    // devices the mesh has found, so it goes here rather than being inferred
    // from a column that cannot mean that. When loss is high the right-hand
    // slot carries the advice instead.
    c.hline(5, SUM_Y - 3, 148, pal::EDGE);
    {
        char b[32]; b[0] = 0; char n[12];
        u2s(n, s.nets24 + s.nets5); cat(b, n); cat(b, " DEVICES");
        c.text(11, SUM_Y, b, pal::BRIGHT);
        if (s.meshLossPct >= 20) {
            c.textRight(155, SUM_Y, "ADD COORD", pal::RED);
        } else if (s.meshLossPct >= 10) {
            c.textRight(155, SUM_Y, "NEAR LIMIT", pal::AMBER);
        } else {
            b[0] = 0;
            u2s(n, s.meshHits); cat(b, n); cat(b, " HITS");
            c.textRight(155, SUM_Y, b, pal::DIM);
        }
    }

    drawFooter(c, s);
    drawReticle(c, s);
    c.vignette();
    c.scanlines();

    if (d) {
        static State prev;
        static bool  first = true;
        d->clear();
        if (first) { d->add(0, 0, W, H); first = false; }
        else {
            bool moved = s.nNodes != prev.nNodes || s.coreCamping != prev.coreCamping ||
                         s.coreFrames != prev.coreFrames ||
                         s.meshLossPct != prev.meshLossPct;
            for (int i = 0; !moved && i < s.nNodes; i++)
                moved = s.nodes[i].recs != prev.nodes[i].recs ||
                        s.nodes[i].ageS != prev.nodes[i].ageS ||
                        s.nodes[i].lossPct != prev.nodes[i].lossPct ||
                        s.nodes[i].live != prev.nodes[i].live;
            if (moved) d->add(0, BODY_Y, W, BODY_H);
            dirtyChrome(*d, s, prev);
        }
        prev = s;
    }
}

} // namespace ui
