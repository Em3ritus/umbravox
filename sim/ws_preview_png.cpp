// Host reference for the designer's Waveshare preview: the same prevws::render() the WebAssembly bridge calls.
//   ws_preview_png THEME SCREEN FRAME SWEEP ALERT_STYLE ALERT_CONF OUT.png
//   ws_preview_png ... OUT --pack DIR   renders the pack in DIR (THEME is then ignored: the pack's id is used)
//   ws_preview_png ... OUT.rgba      writes the raw RGBA bytes the browser gets (for the parity test)
#include "../ui/preview_ws.h"
#include "png_writer.h"
#include "packload.h"
#include <cstdlib>
#include <cstring>
int main(int argc, char** argv) {
    if (argc < 8) { std::fprintf(stderr, "usage: %s THEME SCREEN FRAME SWEEP ALERT_STYLE ALERT_CONF OUT\n", argv[0]); return 2; }
    std::vector<uint16_t> px(prevws::W * prevws::H);
    int theme = atoi(argv[1]);
    if (argc >= 10 && !std::strcmp(argv[8], "--pack")) {
        char why[96] = ""; theme = packload::load(argv[9], why, sizeof why);
        if (theme < 0) { std::fprintf(stderr, "pack refused: %s\n", why); return 3; }
    }
    std::vector<uint8_t> clip; scanr::RadarVideo rv{};       // --video FILE: 4 x int32 LE (w, h, frames, fps), then the packed frames
    for (int i = 8; i + 1 < argc; i++) if (!std::strcmp(argv[i], "--video")) {
        FILE* vf = std::fopen(argv[i + 1], "rb"); if (!vf) { std::fprintf(stderr, "cannot open %s\n", argv[i + 1]); return 4; }
        int32_t hd[4]; if (std::fread(hd, 4, 4, vf) != 4) return 4;
        clip.resize((size_t)hd[2] * ((hd[0] * hd[1] + 1) / 2));
        if (std::fread(clip.data(), 1, clip.size(), vf) != clip.size()) return 4;
        std::fclose(vf);
        rv = scanr::RadarVideo{ hd[0], hd[1], hd[2], hd[3], clip.data() };
        prevws::g_video = &rv;
    }
    prevws::render(theme, atoi(argv[2]), atoi(argv[3]), atoi(argv[4]), atoi(argv[5]), atoi(argv[6]), px.data());
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
