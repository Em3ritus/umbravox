// Pure paced queue for the Core's per-node timing Config sends. ESP-NOW has a
// small transmit queue, and filling it faster than the radio drains it makes
// frames vanish with ESP_OK returned, so the Config for each node goes out one
// at a time, GAP_MS apart, instead of right behind its Assign.
#pragma once
#include <stdint.h>
#include <string.h>
namespace meshtimingq {
constexpr int CAP = 24;
constexpr uint32_t GAP_MS = 15;
struct Queue {
    bool push(const uint8_t* mac) {
        for (int i = 0; i < n_; i++) if (!memcmp(mac_[i], mac, 6)) return true;   // already waiting
        if (n_ >= CAP) return false;                                              // bounded; the next reassign retries
        memcpy(mac_[n_++], mac, 6);
        return true;
    }
    // True and *out filled when one send is due. Spacing uses a signed difference
    // so it holds across the 32-bit wrap and when the clock reads slightly behind.
    bool due(uint32_t now, uint8_t* out) {
        if (!n_) return false;
        if (sent_ && (int32_t)(now - last_) < (int32_t)GAP_MS) return false;
        memcpy(out, mac_[0], 6);
        memmove(mac_[0], mac_[1], (size_t)(n_ - 1) * 6);
        n_--; last_ = now; sent_ = true;
        return true;
    }
    int size() const { return n_; }
private:
    uint8_t mac_[CAP][6]{};
    int n_ = 0;
    uint32_t last_ = 0;
    bool sent_ = false;
};
}
