// The renderer's view of a pack's radar clip (see themepack::Clip, ui/scan_render.h RadarVideo).
//
// pal::clip(theme) says which clip a theme carries; the scan scene holds a POINTER to a RadarVideo for the length of a
// sliced redraw, so the converted struct must outlive it: one per theme id, kept here. Host and device compilable.
#pragma once
#include "palette.h"
#include "scan_render.h"
#include "themepack.h"

namespace radarclip {

using Video = scanr::RadarVideo;

// The clip a theme plays in the wide radar, or nullptr (built-ins and packs without radar.vid).
constexpr int kSlots = pal::TH_COUNT + themepack::MAX_PACKS;     // every theme id there can be (built-ins, then packs)

inline const Video* videoFor(uint8_t theme) {
    static Video cache[kSlots];                                   // ~550 bytes of internal RAM: it is scarce on the board
    if (theme >= kSlots) return nullptr;
    const themepack::Clip* c = pal::clip(theme);
    if (!c || !c->px) return nullptr;
    cache[theme] = Video{ c->w, c->h, c->frames, c->fps, c->px };
    return &cache[theme];
}

} // namespace radarclip
