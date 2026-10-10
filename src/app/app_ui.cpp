#include <app/app_ui.hpp>
#include <app/app_state.hpp>

#include <cstddef>

namespace app_ui {

// One SDL_Texture per registered image; re-uploaded only when the CPU image's
// version changes. Nearest filtering keeps the 8x8 font crisp when the sheet is
// sampled at an integer scale.
static SDL_Texture* ensure_texture(App* e, SDL_Renderer* r, UiTexId id) {
    if (id >= e->ui_images.size()) return nullptr;
    if (e->ui_textures.size() < e->ui_images.size()) {
        e->ui_textures.resize(e->ui_images.size(), nullptr);
        e->ui_tex_versions.resize(e->ui_images.size(), 0);
    }

    const UiImage& img = e->ui_images.get(id);
    SDL_Texture*& tex = e->ui_textures[id];
    int& ver = e->ui_tex_versions[id];

    if (!tex) {
        tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_ABGR8888,
                                SDL_TEXTUREACCESS_STATIC, img.w, img.h);
        if (!tex) return nullptr;
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(tex, SDL_ScaleModeNearest);
        ver = 0;
    }
    if (ver != img.version) {
        SDL_UpdateTexture(tex, nullptr, img.pixels.data(), img.w * (int)sizeof(uint32_t));
        ver = img.version;
    }
    return tex;
}

void submit(App* e, SDL_Renderer* r, const UiDrawList& list) {
    if (list.cmds.empty()) return;

    // Vertices are packed UiVertex { x,y,u,v,rgba }; the command's indices are a
    // contiguous range of the shared index buffer that reference the shared
    // vertex array, so each command is one geometry call over a sub-range.
    for (const UiCmd& c : list.cmds) {
        SDL_Texture* tex = ensure_texture(e, r, c.tex);
        if (!tex) continue;

        SDL_Rect clip;
        clip.x = (int)c.clip_x0;
        clip.y = (int)c.clip_y0;
        clip.w = (int)(c.clip_x1 - c.clip_x0);
        clip.h = (int)(c.clip_y1 - c.clip_y0);
        SDL_RenderSetClipRect(r, &clip);

        SDL_RenderGeometryRaw(
            r, tex,
            &list.vtx[0].x, (int)sizeof(UiVertex),
            reinterpret_cast<const SDL_Color*>(&list.vtx[0].rgba), (int)sizeof(UiVertex),
            &list.vtx[0].u, (int)sizeof(UiVertex),
            (int)list.vtx.size(),
            list.idx.data() + c.idx_offset,
            (int)c.idx_count, 4);
    }

    SDL_RenderSetClipRect(r, nullptr);
}

void shutdown(App* e) {
    for (SDL_Texture* t : e->ui_textures)
        if (t) SDL_DestroyTexture(t);
    e->ui_textures.clear();
    e->ui_tex_versions.clear();
}

}  // namespace app_ui
