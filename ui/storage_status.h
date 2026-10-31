#pragma once
#include <cstdint>
namespace ui {
enum class StorageState:uint8_t { Missing, Ready, Error };
// Mount is last-known state, not a hot-plug probe. A captured writer failure
// stays visible until reboot; subsequent successes must not conceal data loss.
inline StorageState storageState(bool mounted,bool failed){
 return failed?StorageState::Error:mounted?StorageState::Ready:StorageState::Missing;
}
}
