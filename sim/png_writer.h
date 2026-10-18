// Minimal PNG writer: no zlib, no libpng, nothing to install.
//
// Deflate has a "stored" block type that copies bytes verbatim, so a valid
// zlib stream can be produced without compressing anything. The files come out
// bigger than a real encoder would make them, which does not matter for debug
// screenshots and buys a toolchain of exactly `g++`.
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <vector>

namespace png {

inline const uint32_t* crcTable() {
    static uint32_t t[256];
    static bool ready = false;
    if (!ready) {
        for (uint32_t n = 0; n < 256; n++) {
            uint32_t c = n;
            for (int k = 0; k < 8; k++)
                c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            t[n] = c;
        }
        ready = true;
    }
    return t;
}

inline uint32_t crc32(const uint8_t* d, size_t n) {
    const uint32_t* t = crcTable();
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) c = t[(c ^ d[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

inline uint32_t adler32(const uint8_t* d, size_t n) {
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < n; i++) {
        a = (a + d[i]) % 65521u;
        b = (b + a) % 65521u;
    }
    return (b << 16) | a;
}

inline void be32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back((uint8_t)(x >> 24)); v.push_back((uint8_t)(x >> 16));
    v.push_back((uint8_t)(x >> 8));  v.push_back((uint8_t)x);
}

inline void chunk(std::vector<uint8_t>& out, const char* type,
                  const std::vector<uint8_t>& data) {
    be32(out, (uint32_t)data.size());
    std::vector<uint8_t> td;
    for (int i = 0; i < 4; i++) td.push_back((uint8_t)type[i]);
    td.insert(td.end(), data.begin(), data.end());
    out.insert(out.end(), td.begin(), td.end());
    be32(out, crc32(td.data(), td.size()));
}

// RGB565 framebuffer -> RGB888 PNG, nearest-neighbour upscaled by `scale`.
// Nearest-neighbour is the point: the pixels are the art, so they must stay
// hard-edged however large the file is viewed.
inline bool write(const char* path, const uint16_t* px, int w, int h, int scale) {
    if (scale < 1) scale = 1;
    const int ow = w * scale, oh = h * scale;

    std::vector<uint8_t> raw;
    raw.reserve((size_t)oh * (1 + (size_t)ow * 3));
    for (int y = 0; y < oh; y++) {
        raw.push_back(0);                       // filter: none
        const uint16_t* row = px + (size_t)(y / scale) * w;
        for (int x = 0; x < ow; x++) {
            const uint16_t c = row[x / scale];
            const int r5 = (c >> 11) & 0x1F, g6 = (c >> 5) & 0x3F, b5 = c & 0x1F;
            raw.push_back((uint8_t)((r5 * 255 + 15) / 31));
            raw.push_back((uint8_t)((g6 * 255 + 31) / 63));
            raw.push_back((uint8_t)((b5 * 255 + 15) / 31));
        }
    }

    std::vector<uint8_t> z;
    z.push_back(0x78); z.push_back(0x01);       // zlib: deflate, default window
    size_t pos = 0;
    do {
        size_t n = raw.size() - pos;
        if (n > 65535) n = 65535;
        const bool last = (pos + n) >= raw.size();
        z.push_back(last ? 1 : 0);              // BFINAL, BTYPE=00 (stored)
        z.push_back((uint8_t)(n & 0xFF));
        z.push_back((uint8_t)(n >> 8));
        const uint16_t nl = (uint16_t)~(uint16_t)n;
        z.push_back((uint8_t)(nl & 0xFF));
        z.push_back((uint8_t)(nl >> 8));
        z.insert(z.end(), raw.begin() + (long)pos, raw.begin() + (long)(pos + n));
        pos += n;
    } while (pos < raw.size());
    be32(z, adler32(raw.data(), raw.size()));

    std::vector<uint8_t> out = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    std::vector<uint8_t> ihdr;
    be32(ihdr, (uint32_t)ow); be32(ihdr, (uint32_t)oh);
    ihdr.push_back(8);        // bit depth
    ihdr.push_back(2);        // colour type: truecolour
    ihdr.push_back(0); ihdr.push_back(0); ihdr.push_back(0);
    chunk(out, "IHDR", ihdr);
    chunk(out, "IDAT", z);
    chunk(out, "IEND", {});

    FILE* f = fopen(path, "wb");
    if (!f) return false;
    const bool ok = fwrite(out.data(), 1, out.size(), f) == out.size();
    fclose(f);
    return ok;
}

} // namespace png
