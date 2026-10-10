#pragma once

#include <cstdint>
#include <vector>

#include <math/sr_math.hpp>
#include <subsystems/input.hpp>
#include <subsystems/ui/ui_types.hpp>
#include <subsystems/ui/ui_image.hpp>

// =============================================================================
// UiContext — immediate-mode UI in virtual pixels.
//
// All layout is done in a virtual resolution derived from the window
// (ui_scale = max(1, floor(min(win_w/640, win_h/360)))), so the 8x8 font stays
// on an integer scale and looks crisp at any window size. Vertices are emitted
// in window pixels (virtual * scale); the app submits them.
//
// State is minimal, like Dear ImGui: a hot/active/focused id for buttons and a
// clip-rect stack. There is no retained widget tree.
// =============================================================================

struct UiRect { float x, y, w, h; };

enum class UiAlign  { Left, Center, Right };
enum class UiAnchor { TopLeft, Top, TopRight, Center, BottomLeft, Bottom, BottomRight };

class UiContext {
public:
    // Start a frame: recompute the scale, reset the draw list and the clip, and
    // resolve button navigation from the previous frame's button order.
    void begin_frame(const UiInput& in, const UiImages& images, int win_w, int win_h);

    void rect(UiRect r, uint32_t rgba);
    void image(UiRect r, UiTexId tex, uint32_t tint = 0xFFFFFFFFu);
    void text(vec2 pos, const char* s, int scale, uint32_t rgba,
              UiAlign align = UiAlign::Left);
    vec2 text_size(const char* s, int scale) const;

    // True on the frame the button is clicked or activated with accept/nav.
    bool button(int id, UiRect r, const char* label);

    UiRect anchor(UiAnchor a, vec2 size, vec2 margin) const;

    void push_clip(UiRect r);
    void pop_clip();

    // The game asks for a free cursor (menus); app/ applies it.
    void request_pointer(bool visible) { wants_pointer_ = visible; }

    const UiDrawList& end_frame();

    bool  pointer_requested() const { return wants_pointer_; }
    bool  back_pressed() const      { return in_.back; }
    bool  accept_pressed() const    { return in_.accept; }
    float scale() const             { return ui_scale_; }
    vec2  virtual_size() const      { return virtual_size_; }

private:
    void add_quad(UiTexId tex, UiRect r, float u0, float v0, float u1, float v1,
                  uint32_t rgba);
    void set_cmd(UiTexId tex);

    UiDrawList      list_;
    const UiImages* images_ = nullptr;
    UiInput         in_;
    float           ui_scale_ = 1.0f;
    vec2            virtual_size_ = vec2(0.0f, 0.0f);
    int             win_w_ = 0, win_h_ = 0;

    int   cur_cmd_ = -1;
    UiTexId cur_tex_ = 0;
    float clip_[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    std::vector<float> clip_stack_;

    bool wants_pointer_ = false;

    // Button interaction.
    int  hot_ = 0;
    int  active_ = 0;
    int  focus_id_ = 0;
    int  nav_index_ = 0;
    bool accept_frame_ = false;
    std::vector<int> btn_ids_;
    std::vector<int> btn_ids_prev_;
};
