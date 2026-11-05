// Alert beeps through the Waveshare 3.5" speaker (ES8311 codec + NS4150B amplifier). One pattern per confidence
// level (ui/alert_tone.h). A no-op on every other board.
#pragma once
#include <stdint.h>
namespace sound {
// conf: ui::CONF_LOW / MED / HIGH. Returns at once; the beep plays on its own task. A beep already playing wins.
void alert(uint8_t conf);
}
