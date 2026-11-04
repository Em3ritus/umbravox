#pragma once
#include "layout_engine.h"
#include <cstring>
namespace inputevents {
// One resolved action, owned across release and entry. No borrowed record key.
class PendingHit {
public:
    void cancel() { valid_ = false; }
    bool pending() const { return valid_; }
    bool observe(const uieng::HitList& hits, int x, int y, uint32_t epoch) {
        if (valid_) return false;
        for (int i = 0; i < hits.count; ++i) {
            const auto& h = hits.items[i]; const auto& r = h.rect;
            if (x < r.x || x >= r.x+r.w || y < r.y || y >= r.y+r.h) continue;
            hit_ = h; key_[0] = 0;
            if (h.key) { if (std::strlen(h.key) >= sizeof key_) return false; std::strcpy(key_, h.key); }
            hit_.key = h.key ? key_ : nullptr;
            epoch_ = epoch; x_ = x; y_ = y; valid_ = true; return true;
        }
        return false;
    }
    bool take(bool accepts, uint32_t epoch, const uieng::HitList& current, uieng::Hit& out) {
        if (!valid_) return false;
        if (epoch != epoch_) { cancel(); return false; }
        if (!accepts) return false;
        cancel();
        for (int i=0; i<current.count; ++i) {
            const auto& h = current.items[i];
            if (std::strcmp(h.slot, hit_.slot) || h.action != hit_.action) continue;
            if (hit_.key) { if (!h.key || std::strcmp(h.key, key_)) continue; }
            else if (h.key || h.payload != hit_.payload) continue;
            out = h; return true; // same target; current payload for stable record
        }
        return false; // target gone/replaced: never retarget the old coordinate
    }
private:
    uieng::Hit hit_{}; char key_[64]{}; uint32_t epoch_ = 0;
    int x_ = -1, y_ = -1; bool valid_ = false;
};
}
