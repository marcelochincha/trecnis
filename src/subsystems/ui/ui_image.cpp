#include <subsystems/ui/ui_image.hpp>

#include <io/stb_image.hpp>

#include <cstdio>

UiTexId UiImages::add(int w, int h, std::vector<uint32_t> rgba) {
    UiImage img;
    img.w = w;
    img.h = h;
    img.pixels = std::move(rgba);
    img.version = 1;
    images.push_back(std::move(img));
    return (UiTexId)(images.size() - 1);
}

UiTexId UiImages::load_png(const char* path) {
    int w = 0, h = 0, n = 0;
    stbi_uc* data = stbi_load(path, &w, &h, &n, 4);
    if (!data) {
        std::fprintf(stderr, "[ui] could not load '%s': %s\n",
                     path, stbi_failure_reason());
        return 0;
    }

    // stb gives RGBA bytes; pack into 0xAABBGGRR.
    std::vector<uint32_t> px((std::size_t)w * h);
    for (std::size_t i = 0; i < px.size(); ++i) {
        const uint32_t r = data[i * 4 + 0];
        const uint32_t g = data[i * 4 + 1];
        const uint32_t b = data[i * 4 + 2];
        const uint32_t a = data[i * 4 + 3];
        px[i] = (a << 24) | (b << 16) | (g << 8) | r;
    }
    stbi_image_free(data);
    return add(w, h, std::move(px));
}
