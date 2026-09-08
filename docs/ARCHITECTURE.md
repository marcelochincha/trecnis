# ARCHITECTURE.md — análisis del proyecto base para *PingPong RT*

> Basado en el código real de `trecnis/` (rama `master`, commit `52d2a17`) y, donde
> se indica, de `raytrec/` (commit `e586878`). Todo lo que aquí se afirma se
> comprobó en el fuente; lo que **no** existe está marcado como tal.

**Proyecto base recomendado: `trecnis`.** Tiene la separación limpia
`game → RenderScene → Renderer → backends` que el enunciado exige. `raytrec` hace
lo mismo pero con dispatch por `enum` y lógica de backend dispersa en `sr_game.cpp`
y `sr_scene.cpp`; se usará solo como **fuente de piezas** (generadores de escena,
`add_sphere`, cargador OBJ con `.mtl`).

---

## 0. Resumen de una frase

`trecnis` es un **motor de render** (rasterizador + 3 ray tracers tras una
interfaz común) con **un BVH estático + un BVH dinámico reconstruido cada frame**,
y una **demo de escena** (suelo + personaje procedural ondulante + luz puntual
orbitando). No hay juego, ni física, ni colisiones, ni pelota, ni mesa.

---

## 1. Estructura de carpetas real (`trecnis/src/`)

```
main.cpp                    entry point + game loop + SDL window/present
sr_config.hpp               parseo de --width/--height/--fps/--threads/--audio-rate/--debug

core/                       primitivas de bajo nivel, sin dependencia de render/ ni game/
  sr_camera.{hpp,cpp}       cámara: pos, euler ZYX (rad), fov(H,deg), aspect, near/far; view/proj/rot cacheadas
  sr_framebuffer.hpp        framebuffer { uint32_t* colorBuffer; float* depthBuffer; w,h } (ARGB8888)
  sr_geometry.{hpp,cpp}     struct vertex{p,t}, struct triangle{v0,v1,v2}, struct AABB{min,max},
                            struct mesh (vertices+faces+src_vertex+tex+modelMatrix), create_cube/plane/
                            sphere/cylinder/wedge, load_ply_ascii/binary, create_skybox_mesh
  sr_texture.{hpp,cpp}      struct texture{w,h,uint32_t* data(ARGB)}; load_png_texture (usa stb_image)
  sr_render_config.hpp      struct renderConfig{baseColor,tex,lightInfluence,ignoreDepth,ignoreLight,backfaceCull}
  sr_text.{hpp,cpp}         draw_char / draw_text sobre el framebuffer (fuente 8x8 bitmap)
  font8x8_basic.hpp         tabla de datos de la fuente (Daniel Hepper, dominio público)

math/                       header-only, sin dependencias
  sr_math.hpp               incluye los 4 de abajo
  sr_constants.hpp          SR_PI, SR_EPSILON
  sr_vec.hpp                union vec2/vec3/vec4 + operadores + dot/cross/normalize/lerp/minimum/maximum
  sr_lingalg.hpp            struct mat4 (column-major) + operator*, translation/rotation/scaling/transpose,
                            rotationMatrix(pitch,yaw,roll) ZYX, getEulerAngles
  sr_helpers.hpp            to_radians / to_degrees

render/                     el motor. NO conoce el tipo Game.
  render_scene.hpp          struct RenderScene (POD) + struct PointLight + struct RasterItem  ← el "contrato"
  renderer.{hpp,cpp}        struct IRenderBackend (interfaz) + class Renderer (dueño de los backends)
  raster/
    sr_raster.{hpp,cpp}     rasterizador CPU: render_mesh, render_skybox, render_triangle, draw_line,
                            draw_segment_3d, render_gizmo, draw_gizmo_line  (clip 6 planos + scanline + depth)
  raytrace/
    bvh.{hpp,cpp}           ★ class bvh::BVH — build (SAH/Median/Morton) + intersect/occluded + flatten (GPU) + debug_nodes
    accel.hpp               struct ISceneAccel { intersect, occluded } + struct SceneHit  ← interfaz de aceleración
    bvh_accel.{hpp,cpp}     class BvhAccel : ISceneAccel — envuelve {static_bvh, dynamic_bvh, brute_tris}
    cpu_tracer.{hpp,cpp}    class CpuTracer — pool de hilos SDL, traza el frame en franjas horizontales
    sr_raytrace.{hpp,cpp}   vec3 trace_ray(ray, RenderScene, depth) — shading Whitted + GI 1-bounce determinista;
                            SUN_DIR, AMBIENT, SHADOW_EPS (globales); pack(vec3), get_ray_direction()
    sr_ocl.{hpp,cpp}        namespace ocl:: — backend GPU OpenCL; KERNEL_SRC es una REIMPLEMENTACIÓN del shader
    embree_bvh.hpp          embree_ref::Scene — wrapper de Embree (solo si WITH_EMBREE)
    embree_accel.hpp        class EmbreeAccel : ISceneAccel (solo si WITH_EMBREE)

engine/                     capas por encima de core, POR DEBAJO de game
  anim/
    skinned_mesh.{hpp,cpp}  struct SkinnedMesh (skinning rígido 1 hueso/vértice, baked) +
                            build_procedural_character()  ← lo ÚNICO de engine/ que la escena usa
    camera_anim.{hpp,cpp}   load_camera_anim / sample_camera_anim / euler_deg_looking_at  ← COMPILA PERO NADIE LO LLAMA
  assets/
    obj_loader.{hpp,cpp}    load_obj_mesh (OBJ→mesh) / load_obj_tris (OBJ→bvh::Tri con .mtl) / obj_cached_texture
                            ← COMPILA PERO NADIE LO LLAMA en trecnis

game/                       la aplicación
  sr_game.hpp               API pública: game_create/init/update/handle_events/render/shutdown + game_rebuild_static
  sr_game_state.hpp         struct Game (todo el estado; interno, no expuesto)
  sr_game.cpp               construcción de escena, game loop callbacks, input, dispatch de 1 punto
  sr_hud.{hpp,cpp}          HUD de texto, menú de opciones [M], overlays de debug (cajas BVH, normales)

io/
  stb_image.{hpp,cpp}       stb_image v2.x (dominio público) — decodificador PNG/JPG

sound/
  sr_sound.{hpp,cpp}        wrapper mínimo de SDL2_mixer (música + samples). Header casi todo inline.
```

**CMake**: `file(GLOB_RECURSE APP_SOURCES src/*.cpp)` — no hay lista de fuentes;
añadir un `.cpp` basta. Un único target `bvh_raytracer`. `-std=c++17`,
`-march=native -ffast-math`, `Release` por defecto.

---

## 2. Qué hace realmente cada módulo

### Entry point y game loop — `main.cpp` (120 líneas)

```
main():
  global_config = parse_args(argc, argv)         // sr_config.hpp
  init_sdl()                                       // SDL_Init(VIDEO); CreateWindow(RESIZABLE);
                                                   // CreateRenderer(ACCELERATED); RenderSetLogicalSize(w,h);
                                                   // RenderSetIntegerScale(TRUE); CreateTexture(ARGB8888, STREAMING, w,h)
  sound_init(global_config.audio_rate)            // Mix_OpenAudio
  sound_set_music_volume(0.5f)
  game = game_create(w, h)                        // new Game(w,h)  → framebuffer(w,h)
  game_init(game)                                 // carga skybox, construye escena, arranca Renderer + pool
  while (running):
    while (SDL_PollEvent(&e)) game_handle_events(game, e, running)
    [si debug_mode: bucle de single-step con F2]
    game_update(game, dt)                          // anim + rebuild BVH dinámico + input + cámara
    game_render(game, sdl_fb_texture, dt)          // 1 dispatch de render + gizmo + HUD + menú + SDL_UpdateTexture
    SDL_RenderClear/RenderCopy(fb_texture)/RenderPresent
    limitador de frame: SDL_Delay(target_ms - elapsed);  dt = elapsed/1000
  game_shutdown(game)                             // renderer.shutdown(); delete field_mesh; delete character
```

- **`dt`** es tiempo real medido (`SDL_GetTicks64`), con tope inferior por el
  limitador de FPS. Los primeros frames imprimen `dt` por consola.
- El framebuffer es **exactamente** el tamaño de ventana (no hay resolución
  interna). SDL escala la textura a la ventana con integer-scaling.
- **No** se llama a `sound_shutdown()` (fuga menor al cerrar).

### Ventana / SDL

Todo SDL de ventana/presentación vive en `main.cpp`. `sr_game.cpp` usa SDL solo
para **input** (`SDL_GetKeyboardState`, `SDL_GetRelativeMouseState`,
`SDL_SetRelativeMouseMode`, `SDL_Event`), **hilos/semáforos** (vía `CpuTracer`) y
**timing** (`SDL_GetPerformanceCounter`). `SDL_Texture*` se pasa a `game_render`
solo para el `SDL_UpdateTexture` final.

### Cámara — `core/sr_camera.{hpp,cpp}` (ver [CAMERA] en §6)

`struct camera`: `_position`, `_rotation` (euler radianes, orden ZYX
yaw-pitch-roll), `_fov` (HORIZONTAL, en grados), `_aspectRatio`, `_nearPlane`,
`_farPlane`. Matrices `view/projection/rotation` cacheadas con flags dirty.
Setters `setPosition/setRotation/setFov`. `lookAt(target, up)`. La proyección es
perspectiva OpenGL right-handed. Forward = -Z, right = +X, up = +Y.

### Matemáticas — `math/` (header-only)

`vec2/3/4` son `union` (acceso `.x/.y/.z` o `.v[i]`). `mat4` column-major,
`operator()(row,col)`. `rotationMatrix(pitch,yaw,roll)` compone ZYX con **signos
negados** (`-pitch,-yaw,-roll`). No hay quaterniones, no hay `inverse(mat4)`
(solo `transpose`, que se usa como inversa de rotaciones ortonormales).

### Geometría / mallas — `core/sr_geometry.{hpp,cpp}`

`struct mesh`: `vertices` (`vertex{vec3 p; vec2 t}`), `faces` (`triangle{v0,v1,v2}`
índices), `src_vertex` (índice OBJ original por vértice, para binding de rig),
`tex` (puntero prestado), transform `position/rotation/scale` → `modelMatrix()`
cacheada, `double_sided`. Generadores `create_cube/plane/sphere/cylinder/wedge` y
`load_ply_ascii/binary` **existen pero en trecnis solo se usa `create_skybox_mesh`**.

### Materiales

**No hay `struct Material`.** El material vive **en cada triángulo**
(`bvh::Tri`: `albedo, roughness, metallic, ior, emission, tex, uv0/1/2, smooth,
n0/n1/n2`). El rasterizador usa `renderConfig` (color plano o textura). Convertir
una `mesh` a triángulos sombreables se hace a mano en la escena
(`fold_mesh` en `sr_game.cpp`), asignando el material ahí.

### Texturas — `core/sr_texture.*` + `io/stb_image`

`load_png_texture(path, texture&, max_size=256)` decodifica con stb y **reescala a
≤256** (ver salida "resized to 256x256"). ARGB8888. `texture` es dueña de `data`
(delete en destructor).

### Iluminación — `render/raytrace/sr_raytrace.cpp` + `render_scene.hpp`

Modelo Whitted determinista (sin Monte Carlo en CPU): ambiente + sol direccional
(`SUN_DIR` global, `sun_enabled` en RenderScene) + **luces de área** (triángulos
emisivos, tratados como punto en su centroide, con término geométrico) + **luces
puntuales dinámicas** (`PointLight`, sin geometría, caída inversa al cuadrado) +
**GI difusa de 1 rebote determinista** (solo desde el hit primario, `depth==0`,
set fijo de direcciones Hammersley rotadas por hash de posición — sin ruido, sin
denoiser) + **reflexión especular recursiva** (`max_bounces`, Fresnel-Schlick).
Sombras duras (1 rayo de oclusión por luz).

### Framebuffer — `core/sr_framebuffer.hpp`

`new uint32_t[w*h]` + `new float[w*h]`. `clear(color)` pone color y depth=1.
Sin double buffering propio (SDL lo hace).

### Renderer — ver [RENDERER_API.md]. Resumen:

`class Renderer` posee `CpuTracer cpu_` + `vector<IRenderBackend*>` =
`{RasterBackend, CpuBvhBackend, [EmbreeBackend], OpenClBackend}` (índice inicial
`cur_ = 1` → CPU ray tracer). `render(RenderScene&, framebuffer&)` llama al backend
activo. `cycle(dir)` salta al siguiente disponible. `upload_static` / `reload_scene`
empujan geometría estática a los backends que cachean (GPU).

### Ray tracer CPU — `cpu_tracer.*` + `sr_raytrace.*` + `bvh_accel.*`

`CpuTracer` arranca N hilos SDL parados en semáforos; cada `render()` los libera,
cada uno traza una **franja horizontal** `[y0,y1)` llamando `trace_ray` por píxel,
y avisa por un semáforo `done_`. `trace_ray` consulta visibilidad **solo por
`scene.accel`** (`ISceneAccel`), así el mismo shader sirve para BVH propio y para
Embree. `BvhAccel::intersect` combina: BVH dinámico (o brute_tris) + BVH estático,
quedándose con el hit más cercano.

### BVH — ver [BVH_GUIDE.md]. Resumen:

`bvh::BVH::build(vector<Tri>, strategy)` construye top-down: SAH binned (12 bins),
Median (quickselect) o Morton (orden Z global + split por índice). `intersect`
recorre front-to-back con pila fija de 64, descendiendo primero al hijo cercano.
`occluded` = any-hit para sombras. `flatten` exporta 3 arrays planos para la GPU.
Cap de profundidad 60, `MAX_LEAF` 8.

### Backends — OpenCL / Embree / Rasterizador

- **OpenCL** (`sr_ocl.cpp`): `KERNEL_SRC` es un string con **otra implementación**
  del traversal + shading en C-OpenCL. Sube el BVH estático 1 vez, el dinámico
  cada frame (arrays planos de `flatten`), corre 1 work-item/píxel,
  `kOclSamples = 8` fijo. **Cualquier cambio de shading hay que replicarlo aquí.**
- **Embree** (`embree_bvh.hpp`): construye el BVH de Embree sobre los **mismos
  triángulos**; `SAH→HIGH, Median→MEDIUM, Morton→LOW`. Requiere `embree4.lib`
  (build MSVC — riesgo con MinGW). `WITH_EMBREE` default ON pero degrada si falta.
- **Rasterizador** (`sr_raster.cpp`): backend de primera clase. MVP → clip contra
  6 planos (Sutherland-Hodgman) → división perspectiva → Y-flip → backface cull
  (área con signo > 0) → scanline + depth test. Vista de depuración plana.

### Assets / animaciones / sonido / HUD / escenas

- **Assets**: `obj_loader.cpp` (OBJ + .mtl → mesh o tris) **existe pero no se usa**
  en trecnis. En `raytrec` el equivalente (`load_obj` inline en `sr_scene.cpp`) sí
  se usa para mall/cornell/sculpture.
- **Animación**: `SkinnedMesh` (skinning rígido baked, 1 hueso/vértice,
  interpolación lineal entre 2 frames). `SkinnedMesh::load` (rig desde 2 ficheros
  de texto) **no se llama**; solo `build_procedural_character` (columna de 7
  huesos con onda viajera). `camera_anim.cpp` **no se usa**.
- **Sonido**: `sr_sound.hpp` — `sound_init`, `sound_play_music`, `sound_load_sample`,
  `sound_play_sample`, `sound_set_music_volume` (con post-mix amplify > 1.0).
  `main.cpp` llama `sound_init` + `sound_set_music_volume(0.5)` y **nada más**
  (no se reproduce ningún sonido en trecnis).
- **HUD** (`sr_hud.cpp`): `draw_menu` (7 opciones: Backend, Acceleration, Show BVH,
  Reflections, Ray bounces, Static build, Dyn build), `draw_bvh_debug` (cajas
  wireframe hasta `bvh_debug_depth`), `draw_normals_debug`, `tick_ms`.
- **Escenas**: **una sola**, hardcodeada en `sr_game.cpp` (`init_scene` +
  `game_rebuild_static`): `add_box` suelo 16×16 + `build_procedural_character` +
  1 `PointLight` orbitando. `raytrec/src/game/sr_scene.cpp` (680 líneas) tiene 5
  escenas y los helpers `add_sphere/add_box/add_box_c/add_humanoid` que *PingPong
  RT* necesitará.

---

## 3. El contrato Game ⇄ Renderer (real, hoy)

```
Game (sr_game_state.hpp)                       render/  (no conoce Game)
  ┌───────────────────────────┐
  │ framebuffer fb             │               struct RenderScene {          ← render/render_scene.hpp
  │ camera cam                 │                 const camera* cam;
  │ bvh::BVH static_bvh        │                 int width, height;
  │ bvh::BVH dynamic_bvh       │   make_render   const bvh::BVH* static_bvh;
  │ vector<RTTri> rt_tris      │ ─── _scene() ─► const bvh::BVH* dynamic_bvh;
  │ vector<RasterItem>         │                 const vector<Tri>* brute_tris;
  │ vector<PointLight>         │                 bool use_bvh;
  │ array<texture,6> skybox    │                 BuildStrategy static/dynamic_strategy;
  │ mesh* character/field_mesh │                 const ISceneAccel* accel;   (lo pone el backend)
  │ SkinnedMesh skin           │                 const vector<RasterItem>* raster_items;
  │ Renderer renderer          │                 const vector<Tri>* emissive;
  │ flags: reflections,        │                 const vector<PointLight>* point_lights;
  │   max_bounces, gi_*, sun   │                 const array<texture,6>* skybox; bool skybox_enabled;
  └───────────────────────────┘                 vec3 bg_color;
              │                                  bool sun_enabled, reflections; int max_bounces;
              │ renderer.render(scene, fb)       bool gi_enabled; int gi_samples; float gi_strength;
              ▼                                };
   Renderer → backends_[cur_]->render(scene, fb)
                    │
        ┌───────────┼────────────┬──────────────┐
        ▼           ▼            ▼              ▼
   RasterBackend  CpuBvhBackend  EmbreeBackend  OpenClBackend
   (sr_raster)    (CpuTracer +   (CpuTracer +   (ocl:: kernel
                   BvhAccel)      EmbreeAccel)   propio en GPU)
```

**Reglas que ya se cumplen y hay que mantener en *PingPong RT*:**
1. `render/` **nunca** incluye `game/`. El único puente es `RenderScene` (POD de
   punteros prestados).
2. El juego **no** hace ray tracing: llena `RenderScene` y llama `renderer.render`.
3. Todos los backends consumen la **misma geometría**. La única diferencia legítima
   entre ellos es el modelo de luz.
4. El juego **construye** los dos BVH (estático 1 vez / al cambiar escena,
   dinámico cada frame) y pasa punteros; el renderer no los posee.
5. La cámara se pasa **por puntero** dentro de `RenderScene`.

---

## 4. Cómo *PingPong RT* encaja sin tocar el renderer

| Objeto del juego | Geometría | Va al… | Cómo |
|---|---|---|---|
| Suelo | `add_box` (quad) | **BVH estático** | 1 vez en `game_rebuild_static` |
| Pared (oponente) | `add_box` | **BVH estático** | 1 vez |
| Mesa, red (futuro) | `add_box` / OBJ | **BVH estático** | 1 vez |
| Pelota | esfera (`add_sphere` de raytrec, o `create_sphere`) | **BVH dinámico** | cada frame: generar sus tris en su posición actual y meterlos en `rt_tris` |
| Raqueta | box o mesh | **BVH dinámico** | cada frame: `fold_mesh` con su `modelMatrix` |
| Jugador | `build_procedural_character` o OBJ + `SkinnedMesh` | **BVH dinámico** | cada frame: `skin.apply(t)` + `fold_mesh` |

`build_dynamic(e)` ya hace exactamente esto (limpia `rt_tris`, folda el
personaje, `dynamic_bvh.build(rt_tris, Morton)`). *PingPong RT* solo añade más
objetos a `rt_tris` antes del `build`. **Cero cambios en `render/`.**

---

## 5. Diferencias raytrec ↔ trecnis (comparación de dos implementaciones)

| Aspecto | raytrec | trecnis | Para *PingPong RT* |
|---|---|---|---|
| Dispatch de backend | `enum RenderBackend` + `if/else` en `sr_game.cpp` (~80 líneas) | `class Renderer` + `IRenderBackend` polimórfico, 1 punto | **trecnis** |
| Seam game/render | `Game*` entra a `sr_renderer`/`sr_ocl` directamente | `RenderScene` POD, render/ no conoce Game | **trecnis** |
| Escenas | 5 (`sr_scene.cpp`, 680 líneas) + `load_obj` con .mtl | 1 hardcodeada (~60 líneas) | copiar helpers de **raytrec** |
| `add_sphere` | ✅ `sr_scene.cpp:37` | ❌ (solo `create_sphere` en `sr_geometry`, no usado) | **copiar de raytrec** (la pelota) |
| Cargador OBJ+.mtl usado | ✅ inline en `sr_scene.cpp` | ⚠️ `obj_loader.cpp` existe, **nadie lo llama** | wire el de trecnis |
| `PointLight` dinámica | ❌ | ✅ `render_scene.hpp:15` | **trecnis** |
| `--bench` | ✅ (`sr_benchmark.cpp`) | ❌ | no necesario |
| Skinned mesh desde fichero | ✅ (`build_character` carga robloxian + skin.anim) | ⚠️ `SkinnedMesh::load` existe, no se llama | opcional (fase 9) |
| `SkinnedMesh`, `bvh`, `sr_raytrace`, cámara, math, framebuffer | equivalentes | equivalentes | **idénticos, copiar de trecnis** |

---

## 6. Riesgos arquitectónicos (detalle en [MIGRATION_PLAN.md §17])

1. **Shading duplicado CPU/GPU.** `trace_ray` (C++) y `KERNEL_SRC` (OpenCL string)
   implementan el mismo modelo por separado. Añadir materiales/luces obliga a
   tocar los dos o el backend GPU se ve distinto.
2. **`-march=native`.** El binario no corre en otra CPU. Cambiar a `-O3` +
   opcional `-march=native` tras `option(NATIVE_ARCH)`.
3. **BVH dinámico = rebuild completo cada frame** (no hay refit). Barato con
   pocos objetos; vigilar si el jugador es un mesh denso.
4. **`res/` por ruta relativa al CWD.** 6 strings hardcodeados en `game_init`
   (`res/textures/skybox3/...`). Al mover a `assets/` hay que cambiarlos.
5. **Embree con MinGW** (`embree4.lib` es MSVC). Arrancar con `WITH_EMBREE=OFF`.
6. **`file(GLOB_RECURSE)`** requiere re-configurar CMake al añadir `.cpp`
   (`CONFIGURE_DEPENDS` lo mitiga).
7. **El render lee `RenderScene` desde N hilos.** El juego no debe mutar estado de
   escena mientras `renderer.render` corre (hoy no lo hace: update va antes).
