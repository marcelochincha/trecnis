# Trecnis

A simple table-tennis game with custom physics and a real-time **ray-tracing**
renderer. It began as the *raytrec* BVH ray-tracing study and was repurposed
into a single-application game runtime with unified render backends.

## Rendering

Ray tracing is the primary render path; a software rasterizer is kept as a CPU
fallback. The tracer keeps **two BVHs** — a static tree for scenery and a
dynamic tree rebuilt every frame for moving objects (SAH / Median / Morton
build strategies).

Three ray-trace backends sit behind one interface and are switched at runtime
(`[G]`), all compiled in:

| Backend      | Notes                                             |
|--------------|---------------------------------------------------|
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
| `Tab`          | Ray trace ↔ raster                      |
| `G`            | Cycle ray-trace backend                 |
| `B`            | BVH ↔ brute force                       |
| `V` / `N`      | BVH wireframe / normals overlay         |
| `M`            | Options menu                            |
| `H`            | Compact HUD                             |
| `P`            | Play character animation (Character scene) |
| `Esc`          | Close menu / quit                       |

## Layout

```
src/
  core/            framebuffer, camera, geometry, texture, text
  render/
    raytrace/      BVH, acceleration interfaces, CPU tracer, backends
    raster/        software rasterizer (fallback)
    render_scene.hpp
  game/            app: scenes, HUD, skinning
  math/ io/ sound/
  main.cpp
```
