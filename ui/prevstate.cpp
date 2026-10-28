#include "prevstate.h"
#include "nav.h"
#include <new>

#if defined(ESP_PLATFORM)
#include <esp_heap_caps.h>
#include <stdlib.h>
#endif

namespace ui {

State& prevState(uint8_t scr) {
    if (scr >= SCR_COUNT) scr = SCR_SCAN;
#if defined(ESP_PLATFORM)
    static State* slots = nullptr;
    static bool tried = false;
    if (!tried) {
        tried = true;
        void* p = heap_caps_malloc(sizeof(State) * SCR_COUNT, MALLOC_CAP_SPIRAM);
        if (!p) p = malloc(sizeof(State) * SCR_COUNT);
        if (p) {
            slots = static_cast<State*>(p);
            for (int i = 0; i < SCR_COUNT; i++) new (&slots[i]) State();
        }
    }
    if (slots) return slots[scr];
    static State spare;
    return spare;
#else
    static State slots[SCR_COUNT];
    return slots[scr];
#endif
}

void resetPrevStates() {
    for (int i = 0; i < SCR_COUNT; i++) prevState((uint8_t)i) = State();
}

} // namespace ui
