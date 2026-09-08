# CODE_INVENTORY.md — inventario archivo por archivo

> Fuente: `trecnis/src/` salvo filas marcadas `(raytrec)`. Verificado en código.
> **R** = reutilizar tal cual · **A** = adaptar · **N** = no copiar · **C** = crear nuevo.

## Tabla maestra

| Archivo | Función | R | A | N | Dependencias | Rutas fijas |
|---|---|:-:|:-:|:-:|---|---|
| `main.cpp` | entry point + game loop + SDL window/present | | ✅ | | SDL2, `sr_config`, `game/sr_game.hpp`, `sound/sr_sound` | — |
| `sr_config.hpp` | parseo de flags CLI, `inline config global_config` | ✅ | | | `<cstring>`, `<cstdlib>` | — |
| **core/** | | | | | | |
| `core/sr_camera.hpp` / `.cpp` | `struct camera` (pos/euler/fov/aspect/planes; view/proj/rot cacheadas; `lookAt`) | ✅ | | | `math/sr_math.hpp` | — |
| `core/sr_framebuffer.hpp` | `struct framebuffer` (color ARGB + depth float) | ✅ | | | `<cstdint>` | — |
| `core/sr_geometry.hpp` / `.cpp` | `vertex`, `triangle`, `AABB`, `mesh`; `create_*`; `load_ply_*`; `create_skybox_mesh` | | ✅ | | `math/sr_math.hpp`, `<fstream>` | — |
| `core/sr_texture.hpp` / `.cpp` | `struct texture` + `load_png_texture` (reescala ≤256) | ✅ | | | `io/stb_image.hpp` | — |
| `core/sr_render_config.hpp` | `struct renderConfig` (para el rasterizador) | ✅ | | | `math`, `sr_texture` | — |
| `core/sr_text.hpp` / `.cpp` | `draw_char` / `draw_text` sobre framebuffer | ✅ | | | `sr_framebuffer`, `font8x8_basic.hpp` | — |
| `core/font8x8_basic.hpp` | tabla de datos de fuente 8×8 (dominio público) | ✅ | | | — | — |
| **math/** (todo header-only, cero deps) | | | | | | |
| `math/sr_math.hpp` | agregador | ✅ | | | los 4 de abajo | — |
| `math/sr_constants.hpp` | `SR_PI`, `SR_EPSILON` | ✅ | | | — | — |
| `math/sr_vec.hpp` | `vec2/3/4` (union) + operadores + `dot/cross/normalize/lerp/minimum/maximum` | ✅ | | | `<cmath>` | — |
| `math/sr_lingalg.hpp` | `mat4` column-major + `translation/rotation/scaling/transpose` + `rotationMatrix(p,y,r)` ZYX + `getEulerAngles` | ✅ | | | `sr_vec.hpp` | — |
| `math/sr_helpers.hpp` | `to_radians` / `to_degrees` | ✅ | | | `sr_constants`, `sr_vec` | — |
| **render/** | | | | | | |
| `render/render_scene.hpp` | ★ `struct RenderScene` + `PointLight` + `RasterItem` — el contrato game⇄render | | ✅ | | `bvh.hpp`, `accel.hpp`, `sr_camera`, `sr_texture`, `sr_geometry` | — |
| `render/renderer.hpp` / `.cpp` | `struct IRenderBackend` + `class Renderer` (dueño de backends, dispatch 1 punto, `cycle`) | | ✅ | | `render_scene`, `sr_framebuffer`, `cpu_tracer`, todos los backends | — |
| `render/raster/sr_raster.hpp` / `.cpp` (622) | rasterizador CPU: `render_mesh/skybox/triangle`, `draw_line/segment_3d`, `render_gizmo` | ✅ | | | `core/*` | — |
| `render/raytrace/bvh.hpp` / `.cpp` (476) | ★ `class bvh::BVH` — build SAH/Median/Morton, `intersect/occluded`, `flatten` (GPU), `debug_nodes`; `struct Tri` (geometría+material) | ✅ | | | `math/sr_math.hpp`, `sr_geometry.hpp` (AABB) | — |
| `render/raytrace/accel.hpp` | `struct ISceneAccel {intersect, occluded}` + `struct SceneHit` | ✅ | | | `bvh.hpp` | — |
| `render/raytrace/bvh_accel.hpp` / `.cpp` | `class BvhAccel : ISceneAccel` — combina static+dynamic BVH + brute force | ✅ | | | `accel.hpp` | — |
| `render/raytrace/cpu_tracer.hpp` / `.cpp` | `class CpuTracer` — pool de hilos SDL, traza en franjas | ✅ | | | SDL2, `render_scene`, `sr_framebuffer` | — |
| `render/raytrace/sr_raytrace.hpp` / `.cpp` (228) | `trace_ray(ray, RenderScene, depth)` — Whitted + GI 1-bounce + reflexión; `SUN_DIR/AMBIENT/SHADOW_EPS`; `pack`, `get_ray_direction` | | ✅ | | `render_scene.hpp`, `sr_texture.hpp` | — |
| `render/raytrace/sr_ocl.hpp` / `.cpp` (566) | `namespace ocl` — backend GPU; `KERNEL_SRC` = re-impl del shader en OpenCL C | | ✅ | | `CL/cl.h` (WITH_OPENCL) | — |
| `render/raytrace/embree_bvh.hpp` | `embree_ref::Scene` (wrapper Embree, header-only, `#ifdef WITH_EMBREE`) | ✅ | | | `embree4/rtcore.h` | — |
| `render/raytrace/embree_accel.hpp` | `class EmbreeAccel : ISceneAccel` (`#ifdef WITH_EMBREE`) | ✅ | | | `accel.hpp`, `embree_bvh.hpp` | — |
| **engine/** | | | | | | |
| `engine/anim/skinned_mesh.hpp` / `.cpp` (162) | `struct SkinnedMesh` (skinning rígido baked) + `SkinnedMesh::load` (2 ficheros) + `build_procedural_character` | | ✅ | | `sr_geometry.hpp` | (`load()`: rutas que le pases) |
| `engine/anim/camera_anim.hpp` / `.cpp` (65) | `load_camera_anim` / `sample_camera_anim` / `euler_deg_looking_at` — **NADIE LO LLAMA en trecnis** | | ✅ | | `math` | (rutas que le pases) |
| `engine/assets/obj_loader.hpp` / `.cpp` (280) | `load_obj_mesh` (OBJ→mesh), `load_obj_tris` (OBJ+.mtl→`bvh::Tri`), `obj_cached_texture` — **NADIE LO LLAMA en trecnis** | | ✅ | | `sr_geometry`, `sr_texture`, `bvh.hpp` | (rutas que le pases) |
| **game/** | | | | | | |
| `game/sr_game.hpp` | API pública `game_create/init/update/handle_events/render/shutdown` + `game_rebuild_static` | | ✅ | | SDL2 | — |
| `game/sr_game_state.hpp` (92) | `struct Game` (todo el estado del juego) | | ✅ | | `render/*`, `skinned_mesh`, `bvh` | — |
| `game/sr_game.cpp` (396) | construcción de escena (`add_box`, `add_emissive_quad`, `fold_mesh`, `build_dynamic`, `init_scene`), `make_render_scene`, game callbacks, input, dispatch | | ✅ | | todo `render/`, `engine/anim`, `sound`, `sr_hud` | `res/textures/skybox3/*.png` ×6 (líneas 199-204) |
| `game/sr_hud.hpp` / `.cpp` (143) | HUD texto, `draw_menu` (7 items), `draw_bvh_debug`, `draw_normals_debug`, `tick_ms` | | ✅ | | `sr_game_state`, `sr_raster`, `sr_text` | — |
| **io/** | | | | | | |
| `io/stb_image.hpp` (7987) / `.cpp` | stb_image v2.x — decodificador PNG/JPG (dominio público) | ✅ | | | — | — |
| **sound/** | | | | | | |
| `sound/sr_sound.hpp` (123) / `.cpp` | wrapper SDL2_mixer: `sound_init/play_music/load_sample/play_sample/set_music_volume/shutdown` | ✅ | | | `SDL2/SDL_mixer.h` | (rutas que le pases) |
| **(raytrec) — piezas a copiar** | | | | | | |
| `(raytrec) src/game/sr_scene.cpp:37` `add_sphere()` | genera una esfera UV como `bvh::Tri` con normales suaves | | ✅ | | `bvh.hpp` | — |
| `(raytrec) src/game/sr_scene.cpp:61-83` `add_box`/`add_box_c` | caja AABB (variante centro+half) | | ✅ | | `bvh.hpp` | — |
| `(raytrec) src/game/sr_scene.cpp:100` `add_humanoid()` | figura humana de cajas animada por fase (jugador placeholder) | | ✅ | | `bvh.hpp` | — |
| `(raytrec) src/game/sr_scene.cpp:162` `load_obj()` | == `load_obj_tris` de trecnis (mismo código) | | | ✅ | — | usa `dir` del path |
| `(raytrec) src/game/sr_benchmark.cpp` | modo `--bench` | | | ✅ | | | — |
| **CMake / config** | | | | | | |
| `CMakeLists.txt` | ver [BUILD_GUIDE.md] | | ✅ | | | `third_party/`, `lib/`, `bin/`, `res/` |
| `.gitignore` | ignora `third_party/ build/ bin/ *.a *.lib *.dll *.exe out/` | ✅ | | | — | — |

---

## Detalle de los archivos clave

### `render/render_scene.hpp` — el contrato (ADAPTAR: añadir campos si hace falta)

**Contenido:** 3 structs POD.
- `PointLight { vec3 pos; vec3 color; float intensity; }` — luz omni sin geometría.
- `RasterItem { const mesh* geo; uint32_t color; bool shadow; }` — 1 entrada de la lista de dibujo del rasterizador.
- `RenderScene` — 25 campos, todos punteros prestados o PODs (ver [ARCHITECTURE.md §3]).

**Quién lo llena:** `make_render_scene(Game*)` en `sr_game.cpp:155`.
**Quién lo consume:** cada `IRenderBackend::render` y `trace_ray`.
**Usa:** `bvh.hpp`, `accel.hpp`, `sr_camera.hpp`, `sr_texture.hpp`, `sr_geometry.hpp`.
**Copiable directo:** sí; se **adapta** solo si *PingPong RT* necesita pasar datos
nuevos al shader (p. ej. una textura de mesa). Para MVP no hace falta tocar nada.

### `render/renderer.hpp` + `renderer.cpp` — Renderer + backends (ADAPTAR mínimo)

- `IRenderBackend`: `name()`, `available()`, `render(RenderScene&, framebuffer&)`,
  `upload_static(...)`, `on_scene_changed()`.
- `Renderer::init(num_workers, max_bounces, ambient, shadow_eps)` crea, en orden:
  `RasterBackend` (idx 0), `CpuBvhBackend` (idx 1), `EmbreeBackend` (si
  `WITH_EMBREE`), `OpenClBackend`. `cur_ = 1`.
- Backends definidos en `renderer.cpp` en un `namespace {}` anónimo.
**Adaptación:** ninguna obligatoria. Renombrar strings de `name()` si se quiere.
El `OpenClBackend` hardcodea `kOclSamples = 8` (renderer.cpp:159).

### `render/raytrace/bvh.{hpp,cpp}` — ver [BVH_GUIDE.md] (REUTILIZAR tal cual)

`class bvh::BVH` + `struct Tri` (v0/v1/v2, normal, albedo, roughness, metallic,
ior, smooth, n0/n1/n2, emission, uv0/1/2, tex) + `struct TriISect` (cache-hot) +
`struct Hit` + `enum BuildStrategy {SAH, Median, Morton}`.
**Dependencias:** solo `math/sr_math.hpp` y `AABB` de `sr_geometry.hpp`. Muy portable.
**No modificar** (regla del enunciado). Se usa como caja negra.

### `render/raytrace/sr_raytrace.cpp` — shading (ADAPTAR con cuidado)

`SUN_DIR = normalize(vec3(-0.3, 1.0, -0.2))`, `AMBIENT = 0.0f`,
`SHADOW_EPS = 1e-4f` — **globales `const`**, no vienen de `RenderScene`.
`trace_ray`: emisivo → luz directa (sol + área + puntuales) → GI 1-bounce (solo
`depth==0`) → reflexión especular recursiva (Fresnel-Schlick, `spec = fresnel *
(1-roughness)`, corta si `depth >= max_bounces` o `spec <= 0.01` o `!reflections`).
**Riesgo:** cualquier cambio aquí debe replicarse en `KERNEL_SRC` de `sr_ocl.cpp`
o el backend GPU divergirá.

### `engine/anim/skinned_mesh.{hpp,cpp}` — skinning (ADAPTAR)

- `struct SkinnedMesh`: `bind` (rest pose), `vertexBone` (1 int/vértice),
  `skin` (F·B `mat4`), `bones`, `frames`, `fps`, `matched`.
- `SkinnedMesh::load(weights_path, anim_path, mesh)` — carga rig de 2 ficheros de
  texto (formato en el comentario de cabecera). **No se llama en trecnis.**
  `raytrec/src/game/sr_scene.cpp:560` sí lo llama con `res/data/skin.weights` +
  `res/data/skin.anim` + `res/robloxian/robloxian.obj`.
- `SkinnedMesh::apply(mesh&, t)` — escribe la pose a tiempo `t` (segundos),
  interpola linealmente 2 frames, marca `mesh` dirty.
- `build_procedural_character(mesh&, SkinnedMesh&)` — columna de 7 huesos con
  onda viajera baked de 60 frames. **Sin assets.** Es el jugador placeholder de
  la fase 3.

### `game/sr_game.cpp` — la app (ADAPTAR fuerte, es donde entra el gameplay)

Funciones estáticas de escena: `add_box` (línea 27), `add_emissive_quad` (43),
`set_start_view` (54), `make_raster_mesh` (64), `fold_mesh` (81),
`build_dynamic` (97), `game_rebuild_static` (105, expuesta), `init_scene` (128),
`make_render_scene` (155).
Callbacks: `game_create` (189), `game_init` (193), `game_update` (219),
`game_render` (280), `game_handle_events` (351), `game_shutdown` (392).
**Input** (en `game_update`): `SDL_GetKeyboardState` → WASD + Shift/Space;
`SDL_GetRelativeMouseState` → mouse look. **Eventos discretos** (en
`game_handle_events`): ESC, M (menú), rueda (velocidad), TAB/G (backend), B, V,
H, N, L.

### `sound/sr_sound.hpp` — audio (REUTILIZAR)

Casi todo `inline` en el header (el `.cpp` solo hace `#include`). `g_sound`
global `inline`. `sound_init(freq, channels=1, chunk=1024)`. Para efectos de
golpe/rebote: `sound_load_sample(path)` → slot, `sound_play_sample(slot, vol)`.
Máx 8 samples.

---

## Archivos "código muerto" en trecnis (compilan, nadie los usa)

Confirmado por `grep` sobre todo `src/`:

| Archivo / símbolo | Estado | Acción para *PingPong RT* |
|---|---|---|
| `engine/assets/obj_loader.cpp` (`load_obj_mesh`, `load_obj_tris`, `obj_cached_texture`) | compila, 0 llamadas | **Copiar y CABLEAR** — *PingPong RT* lo usará para mesa/raqueta/jugador |
| `engine/anim/camera_anim.cpp` (`load_camera_anim`, `sample_camera_anim`) | compila, 0 llamadas | Copiar; útil en fase 10 (cámara de repetición / intro) |
| `SkinnedMesh::load` | 0 llamadas (solo `build_procedural_character` se usa) | Copiar; cablear en fase 9 si se quiere jugador riggeado |
| `sr_geometry.cpp`: `create_cube/plane/sphere/cylinder/wedge`, `load_ply_ascii/binary` | 0 llamadas (solo `create_skybox_mesh`) | Copiar `create_sphere` (pelota) y `create_cube`; el resto opcional |
| `sr_raster.cpp`: `draw_shadows` (en `RasterBackend`) | **comentado** | dejar comentado |
| raytrec `sr_benchmark.cpp`, modo `--bench` | no está en trecnis | no copiar |
