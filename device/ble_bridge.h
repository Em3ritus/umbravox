#pragma once
#include <stdint.h>
namespace blebridge {
struct Stats {uint8_t mode=0;bool connected=false,streaming=false;uint16_t queued=0;uint32_t rxDrops=0,txRetries=0;
    int32_t pin=-1;};   // pairing code (0..999999) while a phone is pairing, else -1
// Loop-task snapshot; no BLE lifecycle changes.
Stats stats();
const char* deviceName();
// 0 OFF, 1 WarDriveGo, 2 Biscuit. No BLE init/deinit ownership here.
void setMode(uint8_t mode);
uint8_t mode();
void tick(uint32_t now);
const char* status();
}
