#pragma once

#include <cstdint>
#include <vector>

// =============================================================================
// subsystems/ui — a tiny immediate-mode UI that emits vertex geometry.
//
// No SDL, no pixels: the CPU only builds a draw list (a few hundred vertices,
// microseconds); the app rasterizes it through SDL_RenderGeometryRaw, so the
// GPU does the fill, the blending and the scaling. It works with WITH_IMGUI=OFF.
// =============================================================================

// 0 is reserved for the font atlas, which also holds a block of opaque white
// texels so solid rectangles can be drawn from the same texture.
using UiTexId = uint32_t;

// Color is packed 0xAABBGGRR (matches SDL_PIXELFORMAT_ABGR8888).
struct UiVertex {
    float    x, y;
    float    u, v;
    uint32_t rgba;
};

struct UiCmd {
    UiTexId  tex;
    uint32_t idx_offset, idx_count;
    float    clip_x0, clip_y0, clip_x1, clip_y1;   // window pixels
};

struct UiDrawList {
    std::vector<UiVertex> vtx;
    std::vector<uint32_t> idx;
    std::vector<UiCmd>    cmds;

    void clear() { vtx.clear(); idx.clear(); cmds.clear(); }
};
