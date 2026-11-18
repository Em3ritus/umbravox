// packassets.h: the parsed fonts (f0..f7.fnt) and pictures (a0..a15.art) of the theme whose media is in memory.
//
// Pointers only: the bytes belong to whoever loaded them (the device's selection-time loader, the sim's packload,
// the wasm bridge). One theme's set at a time, as on the device. font(i)/art(i) answer only while that theme is
// the active one (pal::currentTheme), so a set left behind by a theme switch is never drawn for another theme.
// A loader clears the set BEFORE it reuses the bytes.
#pragma once
#include <stdint.h>
#include "fontpack.h"
#include "artpack.h"

namespace assets {

constexpr int MAX_FONTS = 8, MAX_ARTS = 16;

void set(uint8_t themeId, const fontpack::Font* const fonts[MAX_FONTS], const artpack::Art* const arts[MAX_ARTS]);
void clear(uint8_t themeId);                   // no-op unless themeId owns the set
const fontpack::Font* font(int i);             // nullptr: out of range, not loaded, or another theme is active
const artpack::Art* art(int i);

}  // namespace assets
