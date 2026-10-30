#pragma once
#include <stdint.h>
namespace ui {
enum class PoiStatus : uint8_t { Ready, Saving, Saved, NoFix, NoCard, WriteError };
inline const char* poiLabel(PoiStatus s) {
    switch(s) {
        case PoiStatus::Saving: return "SAVING";
        case PoiStatus::Saved: return "SAVED";
        case PoiStatus::NoFix: return "NO FIX";
        case PoiStatus::NoCard: return "NO SD";
        case PoiStatus::WriteError: return "ERROR";
        default: return "GPS MARKER";
    }
}
}
