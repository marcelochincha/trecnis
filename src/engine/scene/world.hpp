#pragma once

#include <math/sr_math.hpp>
#include <core/sr_geometry.hpp>
#include <render/render_scene.hpp>   // PointLight
#include <vector>

// =============================================================================
// The world description: what exists this frame, in world space, in gameplay
// terms.
//
// This is the ONLY thing game logic writes. It never sees a BVH, a framebuffer,
// a render backend or SDL — engine/scene turns a World into a RenderScene, and
// app/ turns that into pixels. Adding a ball, a net or a second player means
// pushing one more Entity here, not touching an acceleration structure.
// =============================================================================

// One drawable thing. The transform lives on the mesh itself (mesh::position /
// rotation / scale), so moving an entity is `e.geo->setPosition(p)`.
struct Entity {
    mesh* geo      = nullptr;
    vec3  albedo   = vec3(1.0f, 1.0f, 1.0f);
    float rough    = 0.0f;
    float metallic = 0.0f;   // 0 = dielectric (4% Fresnel head-on), 1 = metal (mirror)
    vec3  emission = vec3(0.0f, 0.0f, 0.0f);  // non-zero makes it an area light
    bool  shadow   = false;   // raster backend: casts a projected planar shadow
};

// Where the camera is this frame. A camera rig writes this; it is the only
// camera concept gameplay needs — no matrices, no aspect ratio, no clip planes.
struct CameraPose {
    vec3  pos   = vec3(0.0f, 1.6f, 6.0f);
    vec3  euler = vec3(0.0f, 0.0f, 0.0f);  // radians: pitch, yaw, roll
    float fov   = 90.0f;                   // horizontal, degrees
};

// Everything that moves. Static scenery is pushed once into the SceneRuntime
// instead (it lives in a separate tree that is not rebuilt per frame).
struct World {
    std::vector<Entity>     dynamic;
    std::vector<PointLight> lights;
    CameraPose              camera;
};
