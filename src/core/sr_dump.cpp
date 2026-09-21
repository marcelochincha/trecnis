#include <core/sr_dump.hpp>
#include <core/sr_framebuffer.hpp>

#include <cctype>
#include <cstdio>
#include <cstdlib>

#ifdef _WIN32
  #include <direct.h>
  #define SR_MKDIR(p) _mkdir(p)
#else
  #include <sys/stat.h>
  #define SR_MKDIR(p) mkdir(p, 0755)
#endif

namespace dump {

std::vector<uint32_t> capture(const framebuffer& fb) {
    const size_t n = (size_t)fb.width * (size_t)fb.height;
    return std::vector<uint32_t>(fb.colorBuffer, fb.colorBuffer + n);
}

uint64_t frame_hash(const framebuffer& fb) {
    uint64_t h = 1469598103934665603ull;          // FNV-1a 64 offset basis
    const size_t n = (size_t)fb.width * (size_t)fb.height;
    for (size_t i = 0; i < n; ++i) {
        const uint32_t px = fb.colorBuffer[i] & 0x00FFFFFFu;
        for (int b = 0; b < 3; ++b) {
            h ^= (uint64_t)((px >> (b * 8)) & 0xFFu);
            h *= 1099511628211ull;                 // FNV-1a 64 prime
        }
    }
    return h;
}

// ---- BMP ------------------------------------------------------------------
// 32-bit BI_RGB, bottom-up. At 32 bpp every row is already 4-byte aligned, so
// there is no padding to compute. The framebuffer stores ARGB8888 in a uint32,
// which on a little-endian host lands in memory as B,G,R,A — exactly the byte
// order BMP wants, so rows are written straight out with no swizzle.

static void put_u16(uint8_t* p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFF); p[1] = (uint8_t)((v >> 8) & 0xFF);
}
static void put_u32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFF);         p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF); p[3] = (uint8_t)((v >> 24) & 0xFF);
}

bool save_bmp(const framebuffer& fb, const char* path) {
    if (fb.width <= 0 || fb.height <= 0) return false;

    const uint32_t pixel_bytes = (uint32_t)fb.width * (uint32_t)fb.height * 4u;
    const uint32_t offset      = 54u;   // 14-byte file header + 40-byte info header

    uint8_t hdr[54] = {};
    hdr[0] = 'B'; hdr[1] = 'M';
    put_u32(hdr + 2,  offset + pixel_bytes);
    put_u32(hdr + 10, offset);
    put_u32(hdr + 14, 40);                        // BITMAPINFOHEADER size
    put_u32(hdr + 18, (uint32_t)fb.width);
    put_u32(hdr + 22, (uint32_t)fb.height);       // positive height = bottom-up
    put_u16(hdr + 26, 1);                         // planes
    put_u16(hdr + 28, 32);                        // bits per pixel
    put_u32(hdr + 30, 0);                         // BI_RGB, uncompressed
    put_u32(hdr + 34, pixel_bytes);

    FILE* f = fopen(path, "wb");
    if (!f) return false;

    bool ok = fwrite(hdr, 1, sizeof(hdr), f) == sizeof(hdr);

    std::vector<uint32_t> row((size_t)fb.width);
    for (int y = fb.height - 1; y >= 0 && ok; --y) {
        const uint32_t* src = fb.colorBuffer + (size_t)y * (size_t)fb.width;
        // Force alpha opaque: viewers that honour the alpha byte would otherwise
        // show a fully transparent image for any backend that leaves it at 0.
        for (int x = 0; x < fb.width; ++x) row[(size_t)x] = src[x] | 0xFF000000u;
        ok = fwrite(row.data(), 4, (size_t)fb.width, f) == (size_t)fb.width;
    }

    fclose(f);
    return ok;
}

// ---- output directory ------------------------------------------------------
// Walks the separators so a nested path ("out/frames") works too. Failures are
// ignored on purpose: the only one that matters is the directory not existing
// afterwards, and fopen reports that.
static void ensure_dir(const char* dir) {
    char buf[512];
    snprintf(buf, sizeof(buf), "%s", dir);
    for (char* p = buf + 1; *p; ++p) {
        if (*p == '/' || *p == '\\') {
            const char c = *p;
            *p = '\0'; SR_MKDIR(buf); *p = c;
        }
    }
    SR_MKDIR(buf);
}

uint64_t write(const framebuffer& fb, const char* dir, const char* tag) {
    ensure_dir(dir);

    char safe[128];
    size_t i = 0;
    for (const char* s = tag; *s && i < sizeof(safe) - 1; ++s, ++i)
        safe[i] = std::isalnum((unsigned char)*s) ? *s : '_';
    safe[i] = '\0';

    char path[640];
    snprintf(path, sizeof(path), "%s/%s.bmp", dir, safe);

    const uint64_t h = frame_hash(fb);
    if (!save_bmp(fb, path)) {
        printf("[dump] FAILED to write %s\n", path);
        fflush(stdout);
        return h;
    }
    printf("[dump] %-34s %4dx%-4d  hash=%016llx\n",
           path, fb.width, fb.height, (unsigned long long)h);
    fflush(stdout);
    return h;
}

DiffStats diff(const std::vector<uint32_t>& a, const std::vector<uint32_t>& b, int tol) {
    DiffStats d;
    const size_t n = a.size() < b.size() ? a.size() : b.size();
    for (size_t i = 0; i < n; ++i) {
        int worst = 0;
        for (int c = 0; c < 3; ++c) {
            const int va = (int)((a[i] >> (c * 8)) & 0xFFu);
            const int vb = (int)((b[i] >> (c * 8)) & 0xFFu);
            const int dv = va > vb ? va - vb : vb - va;
            if (dv > worst) worst = dv;
        }
        if (worst > d.max_delta) d.max_delta = worst;
        if (worst > tol) ++d.pixels;
    }
    return d;
}

} // namespace dump
