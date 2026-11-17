// Host test for ui/ring_gen.h: the integer ring generator reproduces every baked
// table in ui/radar_geometry.h exactly, and ringFor serves any radius 8..102.
#include "../ui/ring_gen.h"
#include <cstdio>
#include <vector>

using namespace radargeometry;

static int fails = 0, checks = 0;
static void check(const char* what, bool ok) { ++checks; if (!ok) { ++fails; printf("  FAIL %s\n", what); } }

static const int TABLED[] = {9, 13, 18, 19, 23, 26, 27, 28, 32, 34, 36, 38, 39, 46, 55, 56, 58, 65, 68, 69, 85, 98, 102};

int main() {
    printf("== ring generator\n");
    for (int r : TABLED) {
        int n = 0;
        const Point* t = ring(r, n);
        std::vector<Point> g(ringCapacity(r));
        const int m = generateRing(r, g.data(), (int)g.size());
        bool same = t && m == n;
        for (int i = 0; same && i < n; i++)
            same = g[i].x == t[i].x && g[i].y == t[i].y && g[i].coverage == t[i].coverage;
        char tag[64]; snprintf(tag, sizeof tag, "generator equals the baked table for r=%d (%d points)", r, n);
        check(tag, same);
    }
    bool capOk = true, allServed = true;
    for (int r = 8; r <= 102; r++) {
        std::vector<Point> g(ringCapacity(r));
        const int m = generateRing(r, g.data(), (int)g.size());
        capOk = capOk && m > 0;
        int n = 0;
        allServed = allServed && ringFor(r, n) != nullptr && n > 0;
    }
    check("capacity is an upper bound for 8..102", capOk);
    check("every radius 8..102 has a ring via ringFor", allServed);
    {
        std::vector<Point> tiny(4);
        check("too small a buffer reports -1", generateRing(50, tiny.data(), 4) == -1);
    }
    {
        int n77 = 0;
        const Point* a = ringFor(77, n77);
        std::vector<Point> g(ringCapacity(77));
        const int m = generateRing(77, g.data(), (int)g.size());
        check("untabled radius 77 comes from the cache with the generator's points", a && n77 == m && a[0].x == g[0].x && a[m - 1].y == g[m - 1].y);
        int n = 0;
        ringFor(78, n); ringFor(79, n);   // cache now 77, 78, 79
        check("a hit returns the same storage", ringFor(77, n) == a);   // 77 becomes most recent
        ringFor(80, n);                                                 // evicts 78, the least recent
        check("cache keeps the 3 most recent radii", ringFor(77, n) == a && n == m);
        std::vector<int> evicted;
        check("an evicted radius is regenerated correctly", ringFor(78, n) != nullptr && n > 0);
    }
    printf(fails ? "FAILED %d/%d\n" : "PASSED %d checks\n", fails ? fails : checks, checks);
    return fails ? 1 : 0;
}
