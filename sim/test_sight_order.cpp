#include "../device/sight_order.h"
#include <cassert>
#include <cstdio>
#include <vector>
int main() {
    using namespace sightorder;
    Entry rows[]={{6,-80},{3,-30},{2,-60},{1,-30},{8,-128},{9,0},{4,127}};
    sort(rows,7);
    const unsigned expected[]={4,9,1,3,2,6,8};
    for(int i=0;i<7;++i) assert(rows[i].slot==expected[i]);
    // Full-table ranking, then pagination: strongest may be discovered last.
    std::vector<Entry> large;
    for(unsigned i=0;i<32768;++i)large.push_back({(uint16_t)i,(int8_t)(-100+(i%80))});
    sort(large.data(),large.size());
    for(unsigned i=1;i<large.size();++i) {
        assert(large[i-1].rssi>=large[i].rssi);
        if(large[i-1].rssi==large[i].rssi)assert(large[i-1].slot<large[i].slot);
    }
    // Page boundaries preserve the same global sequence, including a partial page.
    unsigned visited=0;
    for(unsigned offset=0;offset<7;offset+=3)
        for(unsigned j=offset;j<offset+3 && j<7;++j)
            assert(rows[j].slot==expected[visited++]);
    assert(visited==7);
    // Every identity survives sorting exactly once.
    std::vector<bool> seen(32768,false);
    for(const auto& r:large){assert(!seen[r.slot]);seen[r.slot]=true;}
    Cache c;
    assert(c.due(0,0));c.filter=0;c.at=100;
    assert(!c.due(1099,0));assert(c.due(1100,0));
    assert(c.due(101,1));assert(c.due(101,0,1));
    c.at=0xfffffff0u;assert(!c.due(20,0));assert(c.due(1000,0));
    sort(rows,0);sort(rows,1);
    puts("RSSI order, ties, identities, full-table capacity, refresh and wrap: PASS");
}
