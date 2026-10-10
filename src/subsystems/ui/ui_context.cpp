#include <subsystems/ui/ui_context.hpp>

#include <subsystems/ui/ui_font.hpp>

#include <algorithm>

void UiContext::begin_frame(const UiInput& in, const UiImages& images,
                            int win_w, int win_h) {
    list_.clear();
    images_ = &images;
    in_     = in;
    win_w_  = win_w;
    win_h_  = win_h;

    int s = std::min(win_w / 640, win_h / 360);
    if (s < 1) s = 1;
    ui_scale_ = (float)s;
    virtual_size_ = vec2((float)win_w / ui_scale_, (float)win_h / ui_scale_);

    if (in_.pointer_x >= 0.0f) {
        in_.pointer_x /= ui_scale_;
        in_.pointer_y /= ui_scale_;
    }

    cur_cmd_ = -1;
    clip_[0] = 0.0f; clip_[1] = 0.0f;
    clip_[2] = (float)win_w; clip_[3] = (float)win_h;
    clip_stack_.clear();

    wants_pointer_ = false;
    accept_frame_  = in.accept;
    hot_ = 0;

    // Navigation moves the focus among the buttons declared last frame, in the
    // order they were declared.
    btn_ids_.clear();
    if ((in.nav_up || in.nav_down) && !btn_ids_prev_.empty()) {
        int idx = 0;
        for (std::size_t i = 0; i < btn_ids_prev_.size(); ++i)
            if (btn_ids_prev_[i] == focus_id_) { idx = (int)i; break; }
        const int n = (int)btn_ids_prev_.size();
        idx = in.nav_up ? (idx - 1 + n) % n : (idx + 1) % n;
        focus_id_ = btn_ids_prev_[idx];
    }
}

void UiContext::set_cmd(UiTexId tex) {
    if (cur_cmd_ >= 0) {
        const UiCmd& c = list_.cmds[cur_cmd_];
        if (c.tex == tex && c.clip_x0 == clip_[0] && c.clip_y0 == clip_[1] &&
            c.clip_x1 == clip_[2] && c.clip_y1 == clip_[3])
            return;
    }
    UiCmd c;
    c.tex        = tex;
    c.idx_offset = (uint32_t)list_.idx.size();
    c.idx_count  = 0;
    c.clip_x0 = clip_[0]; c.clip_y0 = clip_[1];
    c.clip_x1 = clip_[2]; c.clip_y1 = clip_[3];
    list_.cmds.push_back(c);
    cur_cmd_ = (int)list_.cmds.size() - 1;
}

void UiContext::add_quad(UiTexId tex, UiRect r, float u0, float v0, float u1,
                         float v1, uint32_t rgba) {
    set_cmd(tex);

    const float x0 = r.x * ui_scale_;
    const float y0 = r.y * ui_scale_;
    const float x1 = (r.x + r.w) * ui_scale_;
    const float y1 = (r.y + r.h) * ui_scale_;

    const uint32_t base = (uint32_t)list_.vtx.size();
    list_.vtx.push_back({ x0, y0, u0, v0, rgba });
    list_.vtx.push_back({ x1, y0, u1, v0, rgba });
    list_.vtx.push_back({ x1, y1, u1, v1, rgba });
    list_.vtx.push_back({ x0, y1, u0, v1, rgba });
    list_.idx.push_back(base + 0); list_.idx.push_back(base + 1);
    list_.idx.push_back(base + 2);
    list_.idx.push_back(base + 0); list_.idx.push_back(base + 2);
    list_.idx.push_back(base + 3);

    list_.cmds[cur_cmd_].idx_count += 6;
}

void UiContext::rect(UiRect r, uint32_t rgba) {
    add_quad(0, r, kUiWhiteU, kUiWhiteV, kUiWhiteU, kUiWhiteV, rgba);
}

void UiContext::image(UiRect r, UiTexId tex, uint32_t tint) {
    add_quad(tex, r, 0.0f, 0.0f, 1.0f, 1.0f, tint);
}

vec2 UiContext::text_size(const char* s, int scale) const {
    if (!s) return vec2(0.0f, 0.0f);
    float max_w = 0.0f, w = 0.0f;
    int lines = 1;
    for (const char* p = s; ; ++p) {
        if (*p == '\0' || *p == '\n') {
            max_w = std::max(max_w, w);
            w = 0.0f;
            if (*p == '\0') break;
            ++lines;
        } else {
            w += (float)kUiGlyphW * scale;
        }
    }
    return vec2(max_w, (float)(kUiGlyphH * scale * lines));
}

void UiContext::text(vec2 pos, const char* s, int scale, uint32_t rgba,
                     UiAlign align) {
    if (!s || rgba == 0) return;
    const float gh = (float)kUiGlyphH * scale;
    const float gw = (float)kUiGlyphW * scale;
    const uint32_t shadow = rgba & 0xFF000000u;

    float y = pos.y;
    const char* p = s;
    while (true) {
        const char* e = p;
        while (*e && *e != '\n') ++e;
        const int len = (int)(e - p);

        float x = pos.x;
        const float lw = len * gw;
        if      (align == UiAlign::Center) x -= lw * 0.5f;
        else if (align == UiAlign::Right)  x -= lw;

        for (int i = 0; i < len; ++i) {
            unsigned char c = (unsigned char)p[i];
            if (c > 127) c = '?';
            const int col = c % kUiGlyphCols;
            const int row = c / kUiGlyphCols;
            const float u0 = (col * kUiGlyphW) / (float)kUiAtlasW;
            const float v0 = (row * kUiGlyphH) / (float)kUiAtlasH;
            const float u1 = (col * kUiGlyphW + kUiGlyphW) / (float)kUiAtlasW;
            const float v1 = (row * kUiGlyphH + kUiGlyphH) / (float)kUiAtlasH;

            const UiRect r{ x + i * gw, y, gw, gh };
            if (shadow) {
                const UiRect rs{ r.x + 1.0f, r.y + 1.0f, r.w, r.h };
                add_quad(0, rs, u0, v0, u1, v1, shadow);
            }
            add_quad(0, r, u0, v0, u1, v1, rgba);
        }

        y += gh;
        if (!*e) break;
        p = e + 1;
    }
}

bool UiContext::button(int id, UiRect r, const char* label) {
    btn_ids_.push_back(id);

    const float px = in_.pointer_x, py = in_.pointer_y;
    const bool hovered = (px >= r.x && px < r.x + r.w &&
                          py >= r.y && py < r.y + r.h);
    if (hovered) {
        hot_ = id;
        focus_id_ = id;   // mouse hover also focuses, so nav picks up from here
    }

    const bool accepted = accept_frame_ && focus_id_ == id;
    const bool clicked  = accepted || (hovered && in_.pointer_pressed);

    uint32_t bg = 0xCC223344;
    if (focus_id_ == id) bg = 0xCC3A66B0;
    if (hovered && in_.pointer_down) bg = 0xCC55557A;

    rect(r, bg);

    const vec2 ts = text_size(label, 1);
    text(vec2(r.x + (r.w - ts.x) * 0.5f, r.y + (r.h - ts.y) * 0.5f),
         label, 1, 0xFFFFFFFFu);
    return clicked;
}

UiRect UiContext::anchor(UiAnchor a, vec2 size, vec2 m) const {
    const float W = virtual_size_.x, H = virtual_size_.y;
    float x = 0.0f, y = 0.0f;
    switch (a) {
        case UiAnchor::TopLeft:     x = m.x;                 y = m.y;                 break;
        case UiAnchor::Top:         x = (W - size.x) * 0.5f; y = m.y;                 break;
        case UiAnchor::TopRight:    x = W - size.x - m.x;    y = m.y;                 break;
        case UiAnchor::Center:      x = (W - size.x) * 0.5f; y = (H - size.y) * 0.5f; break;
        case UiAnchor::BottomLeft:  x = m.x;                 y = H - size.y - m.y;    break;
        case UiAnchor::Bottom:      x = (W - size.x) * 0.5f; y = H - size.y - m.y;    break;
        case UiAnchor::BottomRight: x = W - size.x - m.x;    y = H - size.y - m.y;    break;
    }
    return { x, y, size.x, size.y };
}

void UiContext::push_clip(UiRect r) {
    for (int i = 0; i < 4; ++i) clip_stack_.push_back(clip_[i]);
    clip_[0] = std::max(clip_[0], r.x * ui_scale_);
    clip_[1] = std::max(clip_[1], r.y * ui_scale_);
    clip_[2] = std::min(clip_[2], (r.x + r.w) * ui_scale_);
    clip_[3] = std::min(clip_[3], (r.y + r.h) * ui_scale_);
    cur_cmd_ = -1;
}

void UiContext::pop_clip() {
    if (clip_stack_.size() < 4) return;
    for (int i = 3; i >= 0; --i) {
        clip_[i] = clip_stack_.back();
        clip_stack_.pop_back();
    }
    cur_cmd_ = -1;
}

const UiDrawList& UiContext::end_frame() {
    btn_ids_prev_ = btn_ids_;
    return list_;
}
