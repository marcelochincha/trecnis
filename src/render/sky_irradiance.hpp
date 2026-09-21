#pragma once

#include <math/sr_math.hpp>
#include <core/sr_texture.hpp>
#include <array>

// =============================================================================
// Ambient lighting from the environment.
//
// A flat scalar ambient greys every shadow identically no matter what sky is
// loaded. Projecting the cubemap onto order-2 spherical harmonics costs 9 vec3
// coefficients and one evaluation per shading point, and gives the real thing:
// swap in a sunset skybox and the shadows turn orange by themselves.
//
// Order 2 is not a compromise here. Convolving radiance with a cosine lobe
// annihilates almost everything above band 2 (Ramamoorthi & Hanrahan), so 9
// coefficients reconstruct diffuse irradiance to within ~1% for any sky.
//
// eval() is deliberately header-inline: it runs once per shading point, on the
// hottest path in the renderer. Out-of-line in its own TU it cannot be inlined
// without LTO, and measured ~2x on the whole frame.
// =============================================================================

// Real SH basis, bands 0..2. Projection and reconstruction share it, so the axis
// convention is arbitrary as long as it stays consistent between the two.
inline void sh_basis(const vec3& d, float* Y) {
    Y[0] = 0.282095f;
    Y[1] = 0.488603f * d.y;
    Y[2] = 0.488603f * d.z;
    Y[3] = 0.488603f * d.x;
    Y[4] = 1.092548f * d.x * d.y;
    Y[5] = 1.092548f * d.y * d.z;
    Y[6] = 0.315392f * (3.0f * d.z * d.z - 1.0f);
    Y[7] = 1.092548f * d.x * d.z;
    Y[8] = 0.546274f * (d.x * d.x - d.y * d.y);
}

struct SkyIrradiance {
    vec3 sh[9] = {};
    bool valid = false;

    // Cosine-convolved irradiance arriving at a surface facing `n`, normalised so
    // a uniform sky of radiance 1 evaluates to exactly 1 — the same 0..1 range
    // the sun term uses, so the two are directly comparable.
    vec3 eval(const vec3& n) const {
        if (!valid) return vec3(0.0f, 0.0f, 0.0f);

        // Convolving with a clamped cosine lobe scales each band by one constant:
        // PI, 2PI/3, PI/4. Folded together with the basis constants and the final
        // 1/PI so the whole evaluation is nine multiply-adds and no divisions.
        const float c0 = 0.282095f;              // A0 * Y0  / PI
        const float c1 = 0.325735f;              // A1 * 0.488603 / PI
        const float c2 = 0.273137f;              // A2 * 1.092548 / PI
        const float c3 = 0.078848f;              // A2 * 0.315392 / PI
        const float c4 = 0.136569f;              // A2 * 0.546274 / PI

        float x = n.x, y = n.y, z = n.z;
        vec3 e = sh[0] * c0;
        e = e + sh[1] * (c1 * y);
        e = e + sh[2] * (c1 * z);
        e = e + sh[3] * (c1 * x);
        e = e + sh[4] * (c2 * x * y);
        e = e + sh[5] * (c2 * y * z);
        e = e + sh[6] * (c3 * (3.0f * z * z - 1.0f));
        e = e + sh[7] * (c2 * x * z);
        e = e + sh[8] * (c4 * (x * x - y * y));
        return e;
    }
};

// Project the six cubemap faces into `out`. The face/UV convention MUST stay in
// lockstep with sample_sky() in sr_raytrace.cpp; if that mapping changes and
// this one doesn't, the ambient ends up rotated against the visible background,
// which reads as "the light comes from the wrong side" and is miserable to
// debug. Returns false and marks `out` invalid when no face carries pixels.
bool project_sky_irradiance(const std::array<texture, 6>& faces, SkyIrradiance& out);
