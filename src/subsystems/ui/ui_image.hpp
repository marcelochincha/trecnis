#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <subsystems/ui/ui_types.hpp>

// A CPU-side image registry: pixels in 0xAABBGGRR, plus a version that ticks up
// whenever the pixels change, so the app knows when to re-upload a texture.
struct UiImage {
    int                   w = 0, h = 0;
    std::vector<uint32_t> pixels;
    int                   version = 0;
};

struct UiImages {
    std::vector<UiImage> images;

    // Register a new image, return its id. The id is its index.
    UiTexId add(int w, int h, std::vector<uint32_t> rgba);

    // Load a PNG through src/io's stb_image. Returns 0 (the font atlas id) on
    // failure so a bad path never yields a dangling texture.
    UiTexId load_png(const char* path);

    UiImage&       get(UiTexId id)       { return images[id]; }
    const UiImage& get(UiTexId id) const { return images[id]; }
    std::size_t    size() const          { return images.size(); }
};
