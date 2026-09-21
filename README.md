# Trecnis

A simple table-tennis game with custom physics and a real-time **ray-tracing**
renderer. It began as the *raytrec* BVH ray-tracing study and was repurposed
into a single-application game runtime with unified render backends.

## Rendering

Every render mode is a **backend behind one interface** (`Renderer`), all
compiled in and switched at runtime (`[G]` / `[Tab]`) — there is a single
dispatch point, no raster/raytrace branch. The ray tracers keep **two BVHs** —
a static tree for scenery and a dynamic tree rebuilt every frame for moving
objects (SAH / Median / Morton build strategies).

| Backend      | Notes                                             |
|--------------|---------------------------------------------------|
| Raster       | CPU forward rasterizer, flat-shaded debug view (textured) |
| CPU SAH BVH  | Multithreaded software tracer (worker pool)       |
| Embree       | Intel Embree kernels over the same triangles      |
| OpenCL GPU   | Whole-frame trace on the GPU (if a device exists) |

Game logic writes a `World` (entities, lights, camera pose); `engine/scene`
turns it into a small `RenderScene` view each frame, so `render/` is fully
decoupled from everything above it and nothing above `engine/scene` needs to
know an acceleration structure exists.

## Geometry & Rendering Conventions

All backends consume the **same geometry**. The only legitimate difference
between them is the *lighting model* — the raster is a flat-shaded (optionally
textured) debug view, the ray tracers do global illumination — never face
orientation.

### Coordinate system & handedness
- Right-handed world space, **+Y up**; the camera looks down **-Z** in eye space.
- Projection is a standard right-handed OpenGL perspective (`w = -z_eye`).
- After the perspective divide, NDC x,y ∈ [-1,1]; the raster flips Y when
  mapping to the framebuffer, so screen row 0 is the top.

### Winding & normals (single source of truth)
- **CCW = front-facing.** The canonical face normal is
  `normalize(cross(v1 - v0, v2 - v0))`.
- **Winding is authoritative** — there is no per-mesh normal-flip flag.
  Geometry is authored/loaded with correct CCW winding, so the raster (which
  recomputes the normal from winding) and the ray tracers derive identical
  orientations.
- Meshes visible from both sides set `mesh.double_sided` (floor, shadow quads,
  skybox) instead of flipping winding.

### Backface culling
- Global default **ON** (`renderConfig.backfaceCull`), per-mesh opt-out via
  `mesh.double_sided`.
- **Raster:** discards a triangle when its screen-space signed area is `> 0`
  (front faces project to a *negative* area because `convert_to_fb` flips Y).
- **Ray tracers:** never cull. The shading normal is flipped to face the ray
  (`if (dot(n, dir) > 0) n = -n`), so closed meshes stay watertight (no light
  leaks) and thin geometry is implicitly two-sided.

### Shared frame pipeline

```
   World (what exists)  --SceneRuntime-->  RenderScene (per frame)
                        |
        +---------------+-----------------+
        v                                 v
   RASTER backend                    RAY-TRACE backends
   (flat-shaded debug)               (CPU BVH / Embree / OpenCL)
        |                                 |
   for each mesh:                     build / refit BVH
     MVP transform                    primary rays --> nearest hit
     clip vs 6 planes (Sutherland-Hodgman)      |
     perspective divide               shade: sky + area lights (NEE)
     Y-flip -> screen                        + reflections (bounces)
     backface cull (area > 0)         double_sided -> flip normal to ray
     scanline fill + depth test              |
        v                                 v
              framebuffer (colour + depth)
```

## Build

Bundled dependencies (SDL2, SDL2_mixer, Embree 4, OpenCL) live under
`third_party/` and `lib/`. Requires CMake ≥ 3.16 and a C++17 compiler.

```sh
cmake -S . -B build -G Ninja -DWITH_EMBREE=ON -DWITH_OPENCL=ON
cmake --build build -j
```

Options: `WITH_EMBREE` (default ON), `WITH_OPENCL` (default OFF). Both backends
are optional — when disabled they simply drop out of the runtime toggle.

## Run

```sh
./build/bin/bvh_raytracer.exe          # run from the repo root (needs res/)
# or
cmake --build build --target run
```

Flags: `--width`, `--height`, `--fps`, `--threads <n>` (`-1` = all cores),
`--debug`, `--help`.

## Controls

| Key            | Action                                  |
|----------------|-----------------------------------------|
| `W A S D` + mouse | Fly / look                           |
| `Space` ×2     | Toggle walk / fly                       |
| Mouse wheel    | Move speed                              |
| `G` / `Tab`    | Cycle render backend (next / previous)  |
| `B`            | BVH ↔ brute force                       |
| `V` / `N`      | BVH wireframe / normals overlay         |
| `M`            | Options menu                            |
| `H`            | Compact HUD                             |
| `Esc`          | Close menu / quit                       |

## Layout

```
src/
  core/            framebuffer, camera, geometry, texture, text
  render/
    renderer.{hpp,cpp}  single entry point; owns every backend
    render_scene.hpp    the per-frame view the app fills
    raytrace/      BVH, acceleration interfaces, CPU tracer, ray backends
    raster/        CPU forward rasterizer
  engine/
    input.hpp      InputState: movement/look intent, no devices
    scene/
      world.hpp         Entity / CameraPose / World -- what game logic writes
      scene_runtime.*   owns both BVHs; World -> RenderScene once per frame
    anim/          skinning, procedural character, camera animation
    assets/        OBJ + texture loaders
  app/             host: SDL loop, framebuffer, input mapping, HUD, options
                   menu, backend selection, benchmark harness
  game/
    demo.{hpp,cpp} the placeholder scene: floor, orbiting light, skinned
                   character, free camera. Reads InputState, writes World.
  math/ io/ sound/
  main.cpp
```
