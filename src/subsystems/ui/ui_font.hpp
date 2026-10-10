#pragma once

#include <subsystems/ui/ui_image.hpp>

// Layout of the built-in font atlas. 16x8 cells of 8x8 glyphs cover ASCII 0-127,
// and below them sits an 8x8 block of opaque white for solid rectangles.
constexpr int kUiAtlasW   = 128;
constexpr int kUiAtlasH   = 72;
constexpr int kUiGlyphW   = 8;
constexpr int kUiGlyphH   = 8;
constexpr int kUiGlyphCols = 16;

// UV of the centre of the white block, in the atlas's own units.
constexpr float kUiWhiteU = 4.0f / (float)kUiAtlasW;
constexpr float kUiWhiteV = 68.0f / (float)kUiAtlasH;

// Build the atlas from core/font8x8_basic.hpp and register it as UiTexId 0.
// Idempotent: returns 0 if the registry is not empty.
UiTexId ui_build_font_atlas(UiImages& images);
