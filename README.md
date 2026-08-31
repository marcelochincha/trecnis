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
| Raster       | CPU forward rasterizer (always-available fallback)|
| CPU SAH BVH  | Multithreaded software tracer (worker pool)       |
| Embree       | Intel Embree kernels over the same triangles      |
| OpenCL GPU   | Whole-frame trace on the GPU (if a device exists) |

The game fills a small `RenderScene` view each frame, so `render/` is fully
decoupled from the game logic.

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
    render_scene.hpp    the per-frame view the game fills
    raytrace/      BVH, acceleration interfaces, CPU tracer, ray backends
    raster/        CPU forward rasterizer
  engine/
    anim/          skinning, procedural character, camera animation
    assets/        OBJ + texture loaders
  game/            app: scene, HUD, input
  math/ io/ sound/
  main.cpp
```
