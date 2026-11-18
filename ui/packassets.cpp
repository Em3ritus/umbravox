#include "packassets.h"
#include "palette.h"

namespace assets {
namespace {
// About 100 bytes: small enough for internal RAM.
const fontpack::Font* g_fonts[MAX_FONTS];
const artpack::Art* g_arts[MAX_ARTS];
int g_owner = -1;
}  // namespace

void set(uint8_t themeId, const fontpack::Font* const fonts[MAX_FONTS], const artpack::Art* const arts[MAX_ARTS]) {
    for (int i = 0; i < MAX_FONTS; i++) g_fonts[i] = fonts ? fonts[i] : nullptr;
    for (int i = 0; i < MAX_ARTS; i++) g_arts[i] = arts ? arts[i] : nullptr;
    g_owner = themeId;
}

void clear(uint8_t themeId) {
    if (g_owner != (int)themeId) return;
    for (int i = 0; i < MAX_FONTS; i++) g_fonts[i] = nullptr;
    for (int i = 0; i < MAX_ARTS; i++) g_arts[i] = nullptr;
    g_owner = -1;
}

const fontpack::Font* font(int i) {
    if (i < 0 || i >= MAX_FONTS || g_owner < 0 || g_owner != (int)pal::currentTheme()) return nullptr;
    return g_fonts[i];
}

const artpack::Art* art(int i) {
    if (i < 0 || i >= MAX_ARTS || g_owner < 0 || g_owner != (int)pal::currentTheme()) return nullptr;
    return g_arts[i];
}

}  // namespace assets
