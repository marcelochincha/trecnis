# AGENTS.md

Guide for anyone (human or automated tool) changing this codebase. It is an index:
it says which file to open for which change and which rules must not be broken.
For the user-facing overview, build options and controls, see [README.md](README.md).

Real-time software renderer + ray tracer in C++17 on SDL2. One window, one scene,
four interchangeable render backends. No engine, no external math/render libs —
everything below SDL2 is written here.

---

## Build, run, verify

```sh
cmake -S . -B build -G Ninja            # first time
cmake --build build                     # ~6s incremental
./build/bin/bvh_raytracer.exe           # run from the repo root (needs res/)
./build/bin/bvh_raytracer.exe --bench 3 # headless timings, exits
./build/bin/bvh_raytracer.exe --dump dumps  # one frame per backend + parity report
```

Toolchain: MinGW (`C:/mingw64`) + Ninja. `file(GLOB_RECURSE ... CONFIGURE_DEPENDS)`
picks up new `.cpp` files automatically; adding a source needs no CMake edit,
adding a third-party library does.

CLI flags ([src/sr_config.hpp](src/sr_config.hpp)):
`--width --height --fps --threads --bench N --dump DIR --gi 0|1 --debug --help`.

**There are no unit tests.** The two automated feedback loops are `--bench N` and
`--dump DIR`; use both before claiming a change is good, and say plainly what was
and was not checked (for example "compiled and dumped, pixels not inspected").

### Reading `--dump`

`--dump` advances the sim 30 fixed-dt frames, renders that same frame with every
available backend, writes BMPs plus an FNV-1a hash, and prints a pixel diff of each
tracer against `CPU SOFTWARE`. It is deterministic: a changed hash means a changed
image and nothing else. The two comparisons mean different things:

- **`EMBREE` must match the CPU: 0 pixels, identical hash.** It shares `trace_ray`
  and differs only in traversal, so any difference is a bug.
- **`OCL GPU` is a port target, not a peer.** Shading is prototyped on the CPU and
  ported to the kernel once settled, so the GPU is normally behind and a large
  difference is the expected state, not a regression. It only means something right
  after a deliberate port.

`--gi 0|1` forces indirect lighting off/on at startup, to tell a traversal
disagreement apart from a shading one.

The dump is captured in one place in `app_render`, right after `renderer.render()`
and **before** the gizmo, debug overlays and HUD. The HUD prints per-frame timings,
so including it would make every dump unique. Keep the capture above the overlays.

---

## Architecture

Every layer only knows the one below it.

```
SDL events ──> InputState ──> game_update ──> World
                                                │
                            SceneRuntime::build_frame
                                                │
                                          RenderScene
                                                │
                                    Renderer::render (1 of 4 backends)
                                                │
                                    framebuffer ──> overlays ──> SDL present
```

| Layer | Owns | Must never know about |
|---|---|---|
| [src/game/](src/game/) | scene content, simulation, camera rig | SDL, BVH, framebuffer, backends |
| [src/engine/scene/](src/engine/scene/) | both BVHs, folded tris, skybox, World→RenderScene | SDL, pixels |
| [src/app/](src/app/) | window, pixels, HUD, hotkeys, frame driving | what is *in* the scene |
| [src/render/](src/render/) | turning a RenderScene into pixels | the App type, game logic |

The seam types are the contract: **`World`** (what exists, in gameplay terms) and
**`RenderScene`** (a per-frame read-only view for backends). Swapping the game module
means changing the four `game_*` calls in [app.cpp](src/app/app.cpp) and nothing else.

### The four backends

Registered in order in `Renderer::init()` ([renderer.cpp](src/render/renderer.cpp)),
cycled at runtime with `G` / `TAB`:

| # | Name | Notes |
|---|---|---|
| 0 | `RASTER` | always-available fallback, flat-shaded |
| 1 | `CPU SOFTWARE` | own SAH BVH + `CpuTracer` worker pool → `trace_ray` |
| 2 | `EMBREE` | Intel Embree traversal, **same `trace_ray`** via `ISceneAccel` |
| 3 | `OCL GPU` | separate kernel, own copy of the shading model |

---

## Where things live

**Host**
- [src/main.cpp](src/main.cpp) — SDL init, main loop, frame cap, `run_bench()`
- [src/app/app.cpp](src/app/app.cpp) — frame driving, SDL→`InputState` mapping, hotkeys
- [src/app/app_state.hpp](src/app/app_state.hpp) — the `App` struct, everything the host owns
- [src/app/app_hud.cpp](src/app/app_hud.cpp) — HUD text, options menu, debug overlays
- [src/sr_config.hpp](src/sr_config.hpp) — CLI parsing + `global_config`

**Seams**
- [src/engine/scene/world.hpp](src/engine/scene/world.hpp) — `Entity`, `CameraPose`, `World`
- [src/render/render_scene.hpp](src/render/render_scene.hpp) — `RenderScene`, `PointLight`, `RasterItem`
- [src/engine/scene/scene_runtime.hpp](src/engine/scene/scene_runtime.hpp) — `SceneRuntime`, `RenderOpts`
- [src/engine/input.hpp](src/engine/input.hpp) — `InputState` (intent, not devices)
- [src/render/raytrace/accel.hpp](src/render/raytrace/accel.hpp) — `ISceneAccel`, `SceneHit`

**Rendering**
- [src/render/renderer.hpp](src/render/renderer.hpp), [renderer.cpp](src/render/renderer.cpp) — `IRenderBackend` + `Renderer`, the single dispatch point
- [src/render/raytrace/sr_raytrace.cpp](src/render/raytrace/sr_raytrace.cpp) — **`trace_ray()`: the reference shading model**
- [src/render/raytrace/cpu_tracer.cpp](src/render/raytrace/cpu_tracer.cpp) — worker pool, shared by CPU + Embree
- [src/render/raytrace/bvh.cpp](src/render/raytrace/bvh.cpp), [bvh.hpp](src/render/raytrace/bvh.hpp) — SAH/Median/Morton build, traversal, `Tri`, `flatten()` for the GPU
- [src/render/raytrace/sr_ocl.cpp](src/render/raytrace/sr_ocl.cpp) — OpenCL host code **plus the kernel source inline** (`KERNEL_SRC`, a raw string near the top; there is no `.cl` file)
- [src/render/raster/sr_raster.cpp](src/render/raster/sr_raster.cpp) — scanline rasterizer, skybox, gizmo, debug lines
- [src/render/sky_irradiance.hpp](src/render/sky_irradiance.hpp) — order-2 SH ambient from the cubemap

**Engine / core**
- [src/engine/assets/obj_loader.cpp](src/engine/assets/obj_loader.cpp) — OBJ→`mesh` and OBJ+MTL→`bvh::Tri`
- [src/engine/anim/](src/engine/anim/) — procedural skinned character, keyframed camera paths
- [src/core/](src/core/) — framebuffer, camera, texture, text, geometry, profiler, `sr_dump`
- [src/math/](src/math/) — `vec2/3/4`, `mat4`; [src/io/](src/io/) vendored stb_image; [src/sound/](src/sound/) SDL2_mixer wrapper

Assets: `res/mall/`, `res/cornell/`, `res/data/`.

---

## Invariants — do not break

1. **`trace_ray()` in `sr_raytrace.cpp` is the reference implementation.** Shading
   changes go CPU first, then get ported to the OpenCL kernel. Never the reverse,
   never both at once.
2. **An `ISceneAccel` answers *which triangle*, and gets no vote on the colour.**
   Embree picks the triangle, then `bvh::ray_tri_t()` re-derives `t`, so the hit
   point `P = origin + dir*t` is bit-identical whichever structure found the hit.
   `ray_tri_t()` must stay arithmetically identical to `tri_hit()` in `bvh.cpp`
   (same expressions, same order). Any new accel must re-derive `t` the same way.
   Reason: GI rays start at `P`, nearly tangent to the surface, the worst case for
   precision; a 1-ULP difference in `t` produced visible speckle (1511 px, now 0).
3. **Game logic never sees SDL, a BVH, a framebuffer or a backend.** It reads
   `InputState` and writes `World`. If a change in `game/` needs a render header,
   the design is wrong.
4. **`render/` never includes the `App` type.** It consumes `RenderScene` only.
5. **Two BVHs, on purpose:** static scenery built once (SAH), dynamic geometry
   refolded and rebuilt every frame (Morton). That is what makes skinned meshes
   affordable.
6. **The SH ambient face/UV convention must stay in lockstep with `sample_sky()`**
   in `sr_raytrace.cpp`, or the ambient is rotated against the visible sky ("the
   light comes from the wrong side").
7. **CPU and GPU float math are deliberately matched** (`-ffast-math` ↔
   `-cl-fast-relaxed-math`) so grazing self-intersections round the same way.
8. **`BVH::MAX_DEPTH = 60` and `MAX_LEAF = 8` are load-bearing.** Traversal uses a
   fixed-size stack; unbounded depth corrupts the C stack as an intermittent crash.

---

## Changing shading: CPU first, OpenCL second

Two backends implement the same model in different shapes. Editing both at once
produces divergence nobody notices until someone switches backend. Applies to any
edit to `trace_ray`, `bvh::Tri` material fields, light handling, Fresnel/specular,
GI, or the sky/ambient term.

1. **Tinker on the CPU only.** Work in `trace_ray()`. It is recursive and
   deterministic, so a change either looks right or it does not. Do not touch
   `sr_ocl.cpp` yet. Own BVH vs Embree does not matter here: same shading.
2. **Verify on the CPU.** `--bench 3` (a change that quietly multiplies ray counts
   shows as the close-range rows collapsing in the camera-distance table), then
   `--dump` and look at the pixels. Report honestly whether pixels were inspected.
3. **Port to the kernel** once the CPU version is settled. `KERNEL_SRC` is not
   checked by the C++ compiler; mistakes surface at runtime as a `clBuildProgram`
   failure printed to stdout with an `[ocl]` prefix, so always run and read stdout.
4. **Check parity** with `--dump`: after a deliberate port the OCL diff should
   shrink; Embree must stay at 0 pixels.

How the shapes differ:

| CPU `trace_ray` | OpenCL kernel |
|---|---|
| recursion `trace_ray(..., depth+1)` | `for(depth=0; depth<=MAXD; depth++)`, reassigning `orig`/`dir` |
| returns radiance up the stack | accumulates into `color`, carrying throughput `tp` down |
| **sums** GI bounce + specular reflection | picks **ONE** continuation stochastically, weighted to match in expectation |
| deterministic Hammersley, `gi_samples` dirs | one random cosine-hemisphere dir, `spp` paths |
| `std::pow` / `std::max` | `pown` / `fmax` |

The weighting is the part that breaks: if you change how the CPU weights `local` or
`refl`, the kernel's `tp *= ...` lines must change to match *in expectation*, not
literally.

Constants `AMBIENT`, `SHADOW_EPS` and `max_bounces` reach the kernel as compile-time
`-D` options in `ocl::init()`, not uniforms. Changing them means rebuilding the
OpenCL program; anything that should be adjustable at runtime must become a kernel
argument.

---

## Gotchas

- **LTO is OFF by default, intentionally.** GCC's partitioned LTO on this MinGW
  toolchain shells out to `make`, the sub-make fails, ltrans objects go unbuilt and
  the link still *reports success*. `-DWITH_LTO=ON` uses `-flto-partition=none`;
  check the build log before trusting the binary.
- **`RenderOpts` defaults `ambient_fallback` and `bg_color` to bright green**
  `(0,1,0)`: debug placeholders. That green is what shows with the skybox off.
- **The two `dynamic_strategy` defaults disagree** (`RenderOpts` says SAH,
  `RenderScene` says Morton). `RenderOpts` wins at runtime.
- **With `-DWITH_OPENCL=ON`, roughly 1 run in 4 segfaults inside the AMD driver**
  (`amdocl64.dll`, no frame of ours on the stack). It kills the process before
  stdout is flushed, so a crashed `--dump`/`--bench` looks like empty output: re-run
  it, and use a second build dir with `-DWITH_OPENCL=OFF` for anything unattended.
- Default resolution is **320x220** on purpose; the tracer is CPU-bound.

---

## Which files to touch

| Goal | Touch |
|---|---|
| New object in the scene | `game/game.cpp`: push an `Entity` into `World::dynamic` (moving), or `scene.add_static*()` + `commit_static()` (scenery) |
| New light | `World::lights` (dynamic point light), or an emissive `add_static_quad` (area light, NEE) |
| Change shading / BRDF / GI | `sr_raytrace.cpp`, then `KERNEL_SRC` in `sr_ocl.cpp` (see above) |
| New material property | `bvh::Tri` in `bvh.hpp`, `Entity` in `world.hpp`, the fold in `scene_runtime.cpp`, `BVH::flatten()`, and the kernel's 32-float tri layout (slot `[19]` is a free pad; past 32 floats means changing `TSTRIDE`) |
| New quality / debug toggle | `RenderOpts` → mirror into `RenderScene` in `build_frame()` → read it in the backend; then `menu_label` / `menu_value` / `menu_apply` in `app_hud.cpp` |
| New render backend | Implement `IRenderBackend`, register it in `Renderer::init()` |
| New input | `InputState` in `input.hpp` → map from SDL in `app.cpp` → consume in `game_update` |
| New per-frame timing | Add a `ProfSection` in `sr_profiler.hpp`, wrap the block in `PROF_SCOPE(...)` |

---

## Commits

Keep commits focused, and make sure each one builds. Do not add co-author or
tool-attribution trailers to commit messages or PR descriptions.
