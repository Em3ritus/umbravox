#pragma once
#include <stdint.h>
namespace oui {
// IEEE MA-L registrant, not proof of the device's retail manufacturer.
const char* lookup(const uint8_t mac[6]);
}
