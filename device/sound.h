// Alert beeps through the Waveshare 3.5" speaker (ES8311 codec + NS4150B amplifier). One pattern per confidence
// level (ui/alert_tone.h). A no-op on every other board.
#pragma once
#include <stdint.h>
namespace sound {
// conf: ui::CONF_LOW / MED / HIGH. volume: ui::Settings::sound (0 OFF, 1 QUIET, 2 NORMAL, 3 LOUD; 0 plays nothing).
// Returns at once; the beep plays on its own task, and a request that arrives while one plays replaces any waiting one.
void alert(uint8_t conf, uint16_t volume);
}
