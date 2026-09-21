#include <render/sky_irradiance.hpp>
#include <cmath>

// Centre direction of texel (u, v) on face `f`. Exact inverse of the face
// selection in sample_sky(): faces are ordered -Z, -X, +X, +Z, +Y, -Y.
static vec3 face_dir(int f, float u, float v) {
    float a = 2.0f * u - 1.0f, b = 2.0f * v - 1.0f;
    switch (f) {
        case 2:  return normalize(vec3( 1.0f,  b,     a   ));   // +X
        case 1:  return normalize(vec3(-1.0f,  b,    -a   ));   // -X
        case 4:  return normalize(vec3( a,     1.0f,  b   ));   // +Y
        case 5:  return normalize(vec3( a,    -1.0f, -b   ));   // -Y
        case 3:  return normalize(vec3(-a,     b,     1.0f));   // +Z
        default: return normalize(vec3( a,     b,    -1.0f));   // -Z
    }
}

bool project_sky_irradiance(const std::array<texture, 6>& faces, SkyIrradiance& out) {
    for (int i = 0; i < 9; ++i) out.sh[i] = vec3(0.0f, 0.0f, 0.0f);
    out.valid = false;

    double covered = 0.0;   // accumulated solid angle; approaches 4PI when whole

    for (int f = 0; f < 6; ++f) {
        const texture& t = faces[f];
        if (!t.data || t.width <= 0 || t.height <= 0) continue;

        for (int y = 0; y < t.height; ++y) {
            float v = (y + 0.5f) / (float)t.height;
            float tb = 2.0f * v - 1.0f;

            for (int x = 0; x < t.width; ++x) {
                float u  = (x + 0.5f) / (float)t.width;
                float ta = 2.0f * u - 1.0f;

                // Solid angle of a cube-face texel: the face is a plane at
                // distance 1, so projecting it onto the sphere costs a
                // 1/(1+s^2+t^2)^(3/2) falloff. Without this the corners of every
                // face are over-weighted and the ambient skews toward them.
                float r2 = 1.0f + ta * ta + tb * tb;
                float dw = (2.0f / t.width) * (2.0f / t.height) / (r2 * std::sqrt(r2));

                // ARGB, decoded exactly as sample_face() does. Values are used
                // as-is: the renderer is linear-throughout with no gamma step,
                // so the ambient matches whatever the background already shows.
                uint32_t c = t.data[y * t.width + x];
                vec3 L(((c >> 16) & 0xFF) / 255.0f,
                       ((c >>  8) & 0xFF) / 255.0f,
                       ( c        & 0xFF) / 255.0f);

                float Y[9];
                sh_basis(face_dir(f, u, v), Y);
                for (int i = 0; i < 9; ++i) out.sh[i] = out.sh[i] + L * (Y[i] * dw);

                covered += dw;
            }
        }
    }

    out.valid = covered > 0.0;
    return out.valid;
}
