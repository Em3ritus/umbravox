// MESH -- who is listening, and to what.
//
// Without this the mesh is invisible from the device: a node could be flat,
// out of range, or never assigned, and the only symptom would be a capture
// rate that felt a bit low. The row that matters most is the age column.
#include "screens.h"
#include "palette.h"
#include "font5x7.h"
#include "layout.h"

namespace ui {
namespace {

// Headings occupy the first band, so the rows start below the rule.
constexpr int HEAD_Y = 15, RULE_Y = 23;
constexpr int ROW_Y = 26, ROW_HT = 12, ROWS_N = 5;
constexpr int SUM_Y = 88;    // the totals line

void u2s(char* out, uint32_t v) {
    char tmp[12];
    int n = 0;
    do { tmp[n++] = (char)('0' + v % 10); v /= 10; } while (v && n < 11);
    int i = 0;
    while (n) out[i++] = tmp[--n];
    out[i] = 0;
}
void cat(char* o, const char* s) { int i = 0; while (o[i]) i++; for (int k = 0; s[k]; k++) o[i++] = s[k]; o[i] = 0; }

void ago(char* out, uint32_t s) {
    out[0] = 0; char n[12];
    if (s < 60)        { u2s(n, s);      cat(out, n); cat(out, "S"); }
    else if (s < 3600) { u2s(n, s / 60); cat(out, n); cat(out, "M"); }
    else               { cat(out, "OLD"); }
}

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

void row(Canvas& c, int y, uint16_t mark, const char* id, const char* cover,
         const char* newN, const char* loss, uint16_t lossCol,
         const char* age, uint16_t idCol) {
    c.fill(3, y + 3, 3, 5, mark);
    c.text(8, y + 2, id, idCol);
    c.text(44, y + 2, cover, pal::DIM);
    // NEW is the one worth reading, so it gets the bright ink.
    c.textRight(96, y + 2, newN, pal::BRIGHT);
    c.textRight(128, y + 2, loss, lossCol);
    c.textRight(155, y + 2, age, pal::DIM);
}

} // namespace

void drawMesh(Canvas& c, const State& s, Dirty* d) {
    c.clear(pal::VOID);
    drawHeader(c, s, "MESH");

    c.fill(1, BODY_Y, 158, BODY_H, pal::PANEL);
    c.rect(1, BODY_Y, 158, BODY_H, pal::EDGE);

    // Column headings, because four numbers in a row mean nothing unlabelled.
    c.text(8,  HEAD_Y, "NODE",  pal::EDGE_LIT);
    c.text(44, HEAD_Y, "COVER", pal::EDGE_LIT);
    c.textRight(96,  HEAD_Y, "NEW",  pal::EDGE_LIT);
    c.textRight(128, HEAD_Y, "LOSS", pal::EDGE_LIT);
    c.textRight(155, HEAD_Y, "AGE",  pal::EDGE_LIT);
    c.hline(5, RULE_Y, 148, pal::EDGE);

    char cover[12], age[12], id[12];

    // The Core's own row first. It is a listener too, and when it is camping
    // it is the only thing covering the report channel.
    cover[0] = 0;
    if (s.coreCamping) { cat(cover, "CH"); char n[8]; u2s(n, s.coreChan); cat(cover, n); }
    else               cat(cover, "SWEEP");
    char cnew[12];
    u2s(cnew, s.coreFound);
    // The Core has no link to lose anything over.
    row(c, ROW_Y, pal::WITCH_HI, "CORE", cover, cnew, "-", pal::DIM, "-", pal::BRIGHT);
    c.hline(5, ROW_Y + ROW_HT - 1, 148, Canvas::mix(pal::PANEL, pal::EDGE, 1, 2));

    if (s.nNodes == 0) {
        c.textCenter(W / 2, ROW_Y + ROW_HT + 14, "NO NODES", pal::DIM);
        c.textCenter(W / 2, ROW_Y + ROW_HT + 24, "CORE IS SWEEPING ALONE", pal::EDGE_LIT);
    }

    for (int i = 0; i < s.nNodes && i < ROWS_N - 1; i++) {
        const NodeRow& nd = s.nodes[i];
        const int y = ROW_Y + (i + 1) * ROW_HT;

        // Green while it is reporting, amber once it has gone quiet long
        // enough to be worth looking at, red when it is effectively gone.
        const uint16_t mark = nd.ageS < 10 ? pal::GREEN
                            : nd.ageS < 20 ? pal::AMBER : pal::RED;

        // Last two octets: five columns do not fit with three, and the
        // tail is still enough to tell nodes apart.
        idStr(id, nd.id);
        cover[0] = 0;
        if (nd.camp)       { cat(cover, "CAMP"); }
        else if (nd.nChan) { char n[8]; u2s(n, nd.nChan); cat(cover, n); cat(cover, "CH"); }
        else                 cat(cover, "---");
        ago(age, nd.ageS);
        char nw[12], ls[12];
        u2s(nw, nd.found);
        u2s(ls, nd.lossPct); cat(ls, "%");
        // Under 5% is the air being the air; a fifth missing is a problem.
        const uint16_t lc = nd.lossPct >= 20 ? pal::RED
                          : nd.lossPct >= 5  ? pal::AMBER : pal::GREEN;
        row(c, y, mark, id, cover, nw, ls, lc, age, nd.live ? pal::TEXT : pal::DIM);
        c.hline(5, y + ROW_HT - 1, 148, Canvas::mix(pal::PANEL, pal::EDGE, 1, 2));
    }

    // HITS counts sightings, and an AP beaconing ten times a second racks
    // them up fast -- the number people actually want is how many distinct
    // devices the mesh has found, so it goes here rather than being inferred
    // from a column that cannot mean that.
    c.hline(5, SUM_Y - 3, 148, pal::EDGE);
    {
        char b[32]; b[0] = 0; char n[12];
        u2s(n, s.nets24 + s.nets5); cat(b, n); cat(b, " DEVICES");
        c.text(11, SUM_Y, b, pal::BRIGHT);
        b[0] = 0;
        u2s(n, s.meshHits); cat(b, n); cat(b, " HITS");
        c.textRight(155, SUM_Y, b, pal::DIM);
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
                         s.coreFrames != prev.coreFrames;
            for (int i = 0; !moved && i < s.nNodes; i++)
                moved = s.nodes[i].recs != prev.nodes[i].recs ||
                        s.nodes[i].ageS != prev.nodes[i].ageS ||
                        s.nodes[i].live != prev.nodes[i].live;
            if (moved) d->add(0, BODY_Y, W, BODY_H);
            dirtyChrome(*d, s, prev);
        }
        prev = s;
    }
}

} // namespace ui
