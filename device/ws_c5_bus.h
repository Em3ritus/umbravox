#pragma once
#if defined(WS_C5_35) && defined(UMBRAVOX_SCAN_ENGINE)
#include <Wire.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
namespace wsbus {
inline SemaphoreHandle_t mutex() {
    static SemaphoreHandle_t m = xSemaphoreCreateRecursiveMutex();
    configASSERT(m); return m;
}
// All runtime Wire clients own this across controller restart AND transactions.
// Priority inheritance lets the touch task wait without spinning on the one core.
class Guard {
public:
    Guard() { xSemaphoreTakeRecursive(mutex(), portMAX_DELAY); saved_ = Wire.getTimeOut(); Wire.setTimeOut(5); }
    ~Guard() { Wire.setTimeOut(saved_); xSemaphoreGiveRecursive(mutex()); }
    Guard(const Guard&) = delete;
    Guard& operator=(const Guard&) = delete;
private: uint16_t saved_;
};
}
#else
namespace wsbus { struct Guard {}; }
#endif
