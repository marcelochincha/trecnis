#include <subsystems/ui/ui_font.hpp>

#include <core/font8x8_basic.hpp>

UiTexId ui_build_font_atlas(UiImages& images) {
    if (!images.images.empty()) return 0;

    std::vector<uint32_t> px((std::size_t)kUiAtlasW * kUiAtlasH, 0x00000000u);

    for (int c = 0; c < 128; ++c) {
        const int cx = (c % kUiGlyphCols) * kUiGlyphW;
        const int cy = (c / kUiGlyphCols) * kUiGlyphH;
        for (int y = 0; y < kUiGlyphH; ++y) {
            const uint8_t bits = font8x8_basic[c][y];
            for (int x = 0; x < kUiGlyphW; ++x) {
                if (bits & (1 << x))
                    px[(std::size_t)(cy + y) * kUiAtlasW + (cx + x)] = 0xFFFFFFFFu;
            }
        }
    }

    // White block for solid fills (rows 64..71).
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x)
            px[(std::size_t)(64 + y) * kUiAtlasW + x] = 0xFFFFFFFFu;

    return images.add(kUiAtlasW, kUiAtlasH, std::move(px));
}
