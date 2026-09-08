#pragma once

#include <cmath>
#include <cstdint>
#include <cmath>
#include <core/sr_geometry.hpp>
#include <core/sr_texture.hpp>
#include <core/sr_text.hpp>
#include <core/sr_camera.hpp>
#include <core/sr_render_config.hpp>
#include <core/sr_framebuffer.hpp>

struct rasterCoord
{
    vec3 p; // screen position
    vec3 t; // texture coord
};

struct clipVertex
{
    vec4 pos; // clip-space position
    vec2 uv;  // raw uv (no division by w). Perspective correction is applied later.
};

void draw_line(framebuffer &fb, int x0, int y0, int x1, int y1, uint32_t color, bool use_depth = false);
void render_triangle(framebuffer &fb, rasterCoord cv0, rasterCoord cv1, rasterCoord cv2, const renderConfig &config);
void render_mesh(framebuffer &fb, const camera &cam, const mesh &m, renderConfig &config);
void render_skybox(framebuffer &fb, const camera &cam, std::array<texture, 6> &skyboxTextures);
void draw_gizmo_line(framebuffer &fb, const camera &cam, const vec3 &start, const vec3 &end, uint32_t color);
void render_gizmo(framebuffer &fb, const camera &cam, const vec3 &pos, float size);

// Pixel-by-pixel 3D line segment with depth test + per-pixel color lerp.
// Treats the segment as a real 3D object: gets occluded by anything closer
// in the depth buffer, and writes its own depth so subsequent draws occlude
// it too. `color_a` is at point `a`, `color_b` is at point `b`; the renderer
// linearly interpolates RGB along the screen-space DDA walk.
void draw_segment_3d(framebuffer &fb, const camera &cam,
                     const vec3 &a, const vec3 &b,
                     uint32_t color_a, uint32_t color_b);
