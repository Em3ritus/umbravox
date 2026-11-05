// Host reference for the designer's Waveshare preview: the same prevws::render() the WebAssembly bridge calls.
//   ws_preview_png THEME SCREEN FRAME SWEEP ALERT_STYLE ALERT_CONF OUT.png
//   ws_preview_png ... OUT.rgba      writes the raw RGBA bytes the browser gets (for the parity test)
#include "../ui/preview_ws.h"
#include "png_writer.h"
#include <cstdlib>
#include <cstring>
int main(int argc, char** argv) {
    if (argc < 8) { std::fprintf(stderr, "usage: %s THEME SCREEN FRAME SWEEP ALERT_STYLE ALERT_CONF OUT\n", argv[0]); return 2; }
    std::vector<uint16_t> px(prevws::W * prevws::H);
    prevws::render(atoi(argv[1]), atoi(argv[2]), atoi(argv[3]), atoi(argv[4]), atoi(argv[5]), atoi(argv[6]), px.data());
    const char* out = argv[7]; const size_t n = std::strlen(out);
    if (n > 5 && !std::strcmp(out + n - 5, ".rgba")) {
        FILE* f = std::fopen(out, "wb"); if (!f) return 1;
        for (uint16_t c : px) {   // the same expansion as sim/png_writer.h and the bridge
            const int r5 = (c >> 11) & 0x1F, g6 = (c >> 5) & 0x3F, b5 = c & 0x1F;
            const unsigned char o[4] = { (unsigned char)((r5 * 255 + 15) / 31), (unsigned char)((g6 * 255 + 31) / 63), (unsigned char)((b5 * 255 + 15) / 31), 255 };
            std::fwrite(o, 1, 4, f);
        }
        std::fclose(f); return 0;
    }
    return png::write(out, px.data(), prevws::W, prevws::H, 1) ? 0 : 1;
}
