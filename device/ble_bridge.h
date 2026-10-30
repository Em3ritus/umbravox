#pragma once
#include <stdint.h>
namespace blebridge {
// 0 OFF, 1 WarDriveGo, 2 Biscuit. No BLE init/deinit ownership here.
void setMode(uint8_t mode);
void tick(uint32_t now);
const char* status();
}
