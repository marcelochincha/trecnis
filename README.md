# Trecnis

A real-time software renderer and **ray tracer** in C++17 on SDL2, built as the
runtime for a table-tennis game with custom physics. It began as the *raytrec*
BVH ray-tracing study and was repurposed into a single-application runtime with
unified render backends. Nothing below SDL2 is a library: math, rasterizer, BVH,
tracer and skinning are written here. The current scene is a placeholder (floor,
orbiting light, skinned character, spinning cube) that exists to prove the
architecture; the tennis game will replace it without touching the host.

The default resolution is a deliberately tiny **320x220**, because the tracer is
CPU-bound.

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

**Shading lives in one place.** `trace_ray()` in
`render/raytrace/sr_raytrace.cpp` is the reference shading model, shared by the
CPU and Embree backends, which differ *only* in traversal. The OpenCL kernel
(`KERNEL_SRC`, a raw string in `sr_ocl.cpp`) keeps its own copy and is expected
to lag: shading is prototyped on the CPU and ported to the GPU once settled.

Lighting is a sky ambient term (order-2 spherical harmonics projected from the
cubemap, `render/sky_irradiance.*`), a sun, a dynamic point light, emissive
quads as area lights, and deterministic one-bounce GI (Hammersley samples, so
frames are reproducible).

Game logic writes a `World` (entities, lights, camera pose); `subsystems/scene`
turns it into a small `RenderScene` view each frame, so `render/` is fully
decoupled from everything above it and nothing above `subsystems/scene` needs to
know an acceleration structure exists.

### Invariants worth knowing

- **An acceleration structure decides *which triangle* was hit, never the
  colour.** Embree picks the triangle, then the hit distance is re-derived by
  `bvh::ray_tri_t()`, so the hit point is bit-identical whichever structure found
  it. Without this, a 1-ULP difference in `t` made GI rays diverge (1511 differing
  pixels; now 0). Any new accel must do the same.
- **Two BVHs on purpose:** static scenery built once (SAH), dynamic geometry
  rebuilt every frame (Morton). This is what makes skinned meshes affordable.
- **The SH ambient face/UV convention must match `sample_sky()`**, or the
  ambient ends up rotated against the visible sky.
- **CPU and GPU float math are matched** (`-ffast-math` vs
  `-cl-fast-relaxed-math`) so grazing self-intersections round the same way.
- `BVH::MAX_DEPTH = 60` and `MAX_LEAF = 8` are load-bearing: traversal uses a
  fixed-size stack.

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

Options: `WITH_EMBREE` (default ON), `WITH_OPENCL` (default OFF), `WITH_LTO`
(default OFF), `WIN32_GUI`. Both backends are optional — when disabled they
simply drop out of the runtime toggle. Optimized configs compile at `-O3`
(`/O2 /fp:fast /arch:AVX2` on MSVC).

`WITH_LTO` is off on purpose: on the MinGW toolchain used here, GCC's
partitioned LTO shells out to `make`, the sub-make fails and the link still
reports success. `-DWITH_LTO=ON` uses `-flto-partition=none` to avoid that, but
check the build log before trusting the binary.

With `WITH_OPENCL=ON`, roughly one run in four crashes inside the AMD driver
(`amdocl64.dll`), not in this code. For unattended runs use a second build
directory with OpenCL off.

## Run

```sh
./build/bin/bvh_raytracer.exe          # run from the repo root (needs res/)
# or
cmake --build build --target run
```

Flags: `--width`, `--height`, `--fps`, `--threads <n>` (`-1` = all cores),
`--bench <N>`, `--dump <dir>`, `--gi <0|1>`, `--debug`, `--help`.

## Diagnostics

There are no unit tests. The two automated feedback loops are:

- **`--bench N`** — headless timings, then exits: per-feature cost, a full-frame
  breakdown, a camera-distance sweep and the real capped loop. Use it to check a
  change did not regress performance.
- **`--dump <dir>`** — headless and fully deterministic: advances the sim 30
  fixed-dt frames, renders that same frame with every available backend, writes
  each as a 32-bit BMP, and prints an FNV-1a hash plus a pixel diff of each
  tracer against `CPU SOFTWARE`. A changed hash means a changed image and
  nothing else. `F12` dumps the current frame at runtime to `dumps/`.

Read the two comparisons differently. **Embree must match the CPU exactly (0
pixels)**: it shares `trace_ray`, so any difference is a bug. **OpenCL is a port
target, not a peer**: it is normally behind, and a large difference is the
expected state, meaningful only right after a deliberate port.

The dump is captured right after `renderer.render()` and *before* the gizmo,
debug overlays and HUD, because the HUD prints per-frame timings and would make
every dump unique. Keep it above the overlays.

`--gi 0|1` forces indirect lighting off/on at startup, to tell a traversal
disagreement apart from a shading one without opening the menu.

## Controls

| Key            | Action                                  |
|----------------|-----------------------------------------|
| `W A S D` + mouse | Fly / look                           |
| `Space` ×2     | Toggle walk / fly                       |
| Mouse wheel    | Move speed                              |
| `G` / `Tab`    | Cycle render backend (next / previous)  |
| `B`            | BVH ↔ brute force                       |
| `V` / `N`      | BVH wireframe / normals overlay         |
| `L`            | Toggle sun                              |
| `F12`          | Dump current frame to `dumps/`          |
| `M`            | Options menu                            |
| `H`            | Compact HUD                             |
| `Esc`          | Close menu / quit                       |

## Layout

```
src/
  core/            framebuffer, camera, geometry, texture, text,
                   sr_dump (BMP + hash), sr_profiler (per-frame sections)
  render/
    renderer.{hpp,cpp}  single entry point; owns every backend
    render_scene.hpp    the per-frame view the app fills
    sky_irradiance.*    order-2 SH ambient from the cubemap
    raytrace/      BVH, acceleration interfaces, CPU tracer, ray backends
    raster/        CPU forward rasterizer
  subsystems/      domain subsystems shared by app/ and game/: no loop of
                   their own, no runtime -- construction and conversion only
    input.hpp      InputState: movement/look intent, no devices
    scene/
      world.hpp         Entity / CameraPose / World -- what game logic writes
      scene_runtime.*   owns both BVHs; World -> RenderScene once per frame
    anim/          skinning, procedural character, camera animation
    assets/        OBJ + texture loaders
  app/             host: SDL loop, framebuffer, input mapping, HUD, options
                   menu, backend selection, benchmark harness
  game/
    game.{hpp,cpp} the placeholder scene: floor, orbiting light, skinned
                   character, spinning cube, free camera. Reads InputState,
                   writes World. Never sees SDL, a BVH or a backend.
  math/ io/ sound/
  main.cpp         SDL init, main loop, frame cap, --bench harness
  sr_config.hpp    CLI parsing + global_config
```

Layers only know the one below them: `game/` never sees SDL, a BVH, a
framebuffer or a backend; `render/` never includes the `App` type; `app/` never
knows what is in the scene.

See [AGENTS.md](AGENTS.md) for a contributor/agent-oriented guide (which file to
touch for which change, and the parity workflow for shading edits).
