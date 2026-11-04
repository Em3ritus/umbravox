#pragma once
#include <cstdint>

namespace inputevents {
// Single owner (or externally locked). Edges survive release and rendering.
// Epoch is the destination screen generation, not merely its numeric screen ID.
struct Press { int x = -1, y = -1; uint32_t epoch = 0; };
class PressQueue {
public:
    void navigate() { ++epoch_; head_ = count_ = 0; } // keep held state: no click-through
    uint32_t epoch() const { return epoch_; }
    void sample(bool down, int x, int y, uint32_t sampledEpoch) {
        const bool edge = down && !down_;
        down_ = down;
        if (!edge || sampledEpoch != epoch_) return;
        if (count_ == 8) { ++dropped_; return; } // preserve oldest, expose overflow
        q_[(head_ + count_) % 8] = {x, y, epoch_}; ++count_;
    }
    bool peek(Press& p) const { if (!count_) return false; p = q_[head_]; return true; }
    bool pop(Press& p) {
        if (!count_) return false;
        p = q_[head_]; head_ = (head_ + 1) % 8; --count_; return true;
    }
    void cancel() { head_ = count_ = 0; }
    uint32_t dropped() const { return dropped_; }
private:
    Press q_[8]; unsigned head_ = 0, count_ = 0;
    bool down_ = false; uint32_t epoch_ = 0, dropped_ = 0;
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
