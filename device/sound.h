// Alert beeps through the Waveshare 3.5" speaker (ES8311 codec + NS4150B amplifier). One pattern per confidence
// level (ui/alert_tone.h). A no-op on every other board.
#pragma once
#include <stdint.h>
#include "../ui/palette.h"
#include "../ui/themepack.h"
namespace sound {
// conf: ui::CONF_LOW / MED / HIGH. volume: ui::Settings::sound (0 OFF, 1 QUIET, 2 NORMAL, 3 LOUD; 0 plays nothing).
// Returns at once; the beep plays on its own task, and a request that arrives while one plays replaces any waiting one.
void alert(uint8_t conf, uint16_t volume, const int16_t* pcm = nullptr, uint32_t n = 0);   // pcm: a pack's own sound (16 kHz mono), else the built-in beep
// True while the speaker task is reading a pack's samples, or one is queued. A pack's sounds live in a buffer that is
// overwritten when another theme is selected (device/themeload.cpp), so the loader waits for this to go false first.
bool playing();
// The sound for this theme and level: the pack's own alert.snd for that level if it has one, else the built-in beep.
inline void alertFor(uint8_t theme, uint8_t conf, uint16_t volume) {
    const themepack::Sound* s = pal::sound(theme);
    const int k = conf > 2 ? 2 : conf;
    if (s && s->n[k]) alert(conf, volume, s->pcm[k], s->n[k]); else alert(conf, volume);
}
}
