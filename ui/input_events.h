#pragma once
#include <cstdint>

namespace inputevents {
// Single owner (or externally locked). Edges survive release and rendering.
// Epoch is the destination screen generation, not merely its numeric screen ID.
struct Press { int x = -1, y = -1; uint32_t epoch = 0; bool hold = false; };   // hold: finger stayed down on the hold target
class PressQueue {
public:
    static constexpr uint32_t HOLD_MS = 600;
    // One rectangle (the header bridge chip) where a touch means "tap" OR "hold". A touch that
    // starts inside it does not fire on touch-down: it fires as an ordinary tap on release, or as
    // one hold event once the finger has stayed down for HOLD_MS. Everywhere else a tap still
    // fires on touch-down, exactly as before.
    void setHold(int x, int y, int w, int h) { hx_ = x; hy_ = y; hw_ = w; hh_ = h; holdOn_ = w > 0 && h > 0; }
    void navigate() { ++epoch_; head_ = count_ = 0; deferred_ = false; } // keep held state: no click-through
    uint32_t epoch() const { return epoch_; }
    void sample(bool down, int x, int y, uint32_t sampledEpoch, uint32_t now = 0) {
        const bool edge = down && !down_;
        const bool release = !down && down_;
        down_ = down;
        if (release) {
            if (deferred_) { deferred_ = false; push({dx_, dy_, epoch_, false}); } // a tap: finger lifted before the hold
            return;
        }
        if (edge && sampledEpoch == epoch_) {
            if (holdOn_ && inside(x, y)) { deferred_ = true; dx_ = x; dy_ = y; downAt_ = now; return; }
            push({x, y, epoch_, false});
            return;
        }
        if (down && deferred_) {
            if (!inside(x, y)) deferred_ = false;                              // slid off: neither tap nor hold
            else if ((int32_t)(now - downAt_) >= (int32_t)HOLD_MS) { deferred_ = false; push({x, y, epoch_, true}); }
        }
    }
    bool peek(Press& p) const { if (!count_) return false; p = q_[head_]; return true; }
    bool pop(Press& p) {
        if (!count_) return false;
        p = q_[head_]; head_ = (head_ + 1) % 8; --count_; return true;
    }
    void cancel() { head_ = count_ = 0; }
    uint32_t dropped() const { return dropped_; }
private:
    bool inside(int x, int y) const { return x >= hx_ && y >= hy_ && x < hx_ + hw_ && y < hy_ + hh_; }
    void push(const Press& p) {
        if (count_ == 8) { ++dropped_; return; } // preserve oldest, expose overflow
        q_[(head_ + count_) % 8] = p; ++count_;
    }
    Press q_[8]; unsigned head_ = 0, count_ = 0;
    bool down_ = false; uint32_t epoch_ = 0, dropped_ = 0;
    bool holdOn_ = false, deferred_ = false; int hx_ = 0, hy_ = 0, hw_ = 0, hh_ = 0, dx_ = 0, dy_ = 0; uint32_t downAt_ = 0;
};
// Absolute periodic release; no catch-up storm after a blocked bus transaction.
class Deadline {
public:
    static constexpr uint32_t PERIOD_MS = 20;
    uint32_t complete(uint32_t now) {
        if (!started_) { started_ = true; next_ = now + 2 * PERIOD_MS; return PERIOD_MS; }
        if ((int32_t)(now - next_) >= 0) { ++missed_; next_ = now + PERIOD_MS; }
        const uint32_t wait = next_ - now; next_ += PERIOD_MS; return wait;
    }
    uint32_t missed() const { return missed_; }
private:
    bool started_ = false; uint32_t next_ = 0, missed_ = 0;
};
} // namespace inputevents
