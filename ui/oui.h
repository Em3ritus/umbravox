#pragma once
#include <stdint.h>
#include <stddef.h>
namespace oui {
enum class Registry : uint8_t {None, MaL, MaM, MaS, Iab};
struct Match {const char* vendor="UNKNOWN";uint64_t prefix=0;uint8_t bits=0;Registry registry=Registry::None;};
// Registered organization, not proof of retail manufacturer or product identity.
// Caller must not pass BLE random addresses as if they were public EUI-48.
Match identify(const uint8_t mac[6]);
const char* lookup(const uint8_t mac[6]);
const char* registryName(Registry registry);
void formatPrefix(const Match& match,char* out,size_t cap);
const char* snapshotDate();
}
