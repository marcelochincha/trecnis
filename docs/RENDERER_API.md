# RENDERER_API.md — el motor de render y su contrato

> Todo verificado en `trecnis/src/render/`. Firmas copiadas del código real.

## 0. El pipeline conceptual (lo que el enunciado pide conservar)

```
Game (game/)                                          [conoce las reglas del juego]
  │  cada frame: mueve objetos, integra física, resuelve colisiones,
  │              (re)construye static_bvh (raro) y dynamic_bvh (siempre)
  ▼
RenderScene (render/render_scene.hpp)                 [POD de punteros prestados; NADIE lo posee]
  │  make_render_scene(Game*) lo rellena
  ▼
Renderer (render/renderer.hpp)                        [dueño del pool de hilos y de los backends]
  │  renderer.render(scene, fb) → backends_[cur_]->render(scene, fb)
  ▼
Backend activo                                        [Raster | CPU BVH | Embree | OpenCL]
  │  Raster:  MVP + clip + scanline + depth
  │  CPU/Embree: CpuTracer (pool) → trace_ray() por píxel vía ISceneAccel
  │  OpenCL: sube arrays planos, 1 work-item/píxel en la GPU
  ▼
framebuffer (color ARGB8888 + depth float)            → SDL_UpdateTexture en main.cpp
```

**Invariante:** `render/` **no** hace `#include` de `game/`. Comprobado: el único
tipo compartido es `RenderScene` + lo que este referencia (`camera`, `bvh::BVH`,
`bvh::Tri`, `texture`, `mesh`, `PointLight`, `RasterItem`).

**Excepción menor en la dirección contraria:** `game/sr_game.cpp` **sí** incluye
`render/raytrace/sr_ocl.hpp` — solo para pintar en el HUD `ocl::available()` y
`ocl::device_name()` (sr_game.cpp:6, 307-308). Para *PingPong RT* conviene cerrar
esa fuga añadiendo `Renderer::current_device_name()` y que el juego use solo
`render/renderer.hpp` + `render/render_scene.hpp` + `render/raytrace/bvh.hpp`.

---

## 1. `struct RenderScene` — el contrato completo (render_scene.hpp)

```cpp
struct PointLight {                       // luz omni dinámica, sin geometría
    vec3  pos       = {0,3,0};
    vec3  color     = {1,1,1};
    float intensity = 10.0f;             // contribución = color*intensity / dist^2
};

struct RasterItem {                       // 1 malla para el rasterizador
    const mesh* geo    = nullptr;
    uint32_t    color  = 0xFFFFFFFF;      // color plano si geo->tex == null
    bool        shadow = false;           // (sombra proyectada — hoy desactivada)
};

struct RenderScene {
    const camera* cam = nullptr;
    int width = 0, height = 0;

    // --- geometría ---
    const bvh::BVH*              static_bvh  = nullptr;   // escenario (build raro)
    const bvh::BVH*              dynamic_bvh = nullptr;   // objetos móviles (build cada frame)
    const std::vector<bvh::Tri>* brute_tris = nullptr;    // lista plana dinámica (brute force / fuente Embree dyn)
    bool use_bvh = true;                                  // false → fuerza bruta O(N)
    bvh::BuildStrategy static_strategy  = bvh::SAH;
    bvh::BuildStrategy dynamic_strategy = bvh::Morton;
    const ISceneAccel* accel = nullptr;   // lo setea el backend justo antes de trazar (no lo pone el juego)

    // --- rasterizador ---
    const std::vector<RasterItem>* raster_items = nullptr;

    // --- luces / entorno ---
    const std::vector<bvh::Tri>*   emissive     = nullptr;  // triángulos-luz (NEE)
    const std::vector<PointLight>* point_lights = nullptr;
    const std::array<texture,6>*   skybox       = nullptr;
    bool skybox_enabled = true;
    vec3 bg_color = {0,0,0};               // color de miss cuando skybox off (y clear del raster)

    // --- opciones de shading ---
    bool sun_enabled = true;
    bool reflections = true;
    int  max_bounces = 1;
    bool  gi_enabled  = true;              // GI difusa 1 rebote determinista
    int   gi_samples  = 4;
    float gi_strength = 1.0f;
};
```

Todos los `const T*` son **prestados**: el `Game` los mantiene vivos durante todo
el frame. `accel` es la única excepción: lo escribe el backend (`CpuBvhBackend`/
`EmbreeBackend`) sobre su propia estructura antes de llamar a `CpuTracer::render`.

---

## 2. Cómo se crea un RenderScene (sr_game.cpp:155, `make_render_scene`)

```cpp
static RenderScene make_render_scene(Game* e) {
    RenderScene s;
    s.cam = &e->cam;  s.width = e->fb.width;  s.height = e->fb.height;

    s.static_bvh  = &e->static_bvh;          // punteros a miembros de Game
    s.dynamic_bvh = &e->dynamic_bvh;
    s.brute_tris  = &e->rt_tris;
    s.use_bvh     = e->use_bvh;
    s.static_strategy  = e->build_strategy;
    s.dynamic_strategy = e->dynamic_build_strategy;

    e->raster_items.clear();                 // se reconstruye cada frame
    if (e->field_mesh) e->raster_items.push_back({ e->field_mesh, pack(e->floor_albedo), false });
    if (e->character)  e->raster_items.push_back({ e->character,  pack(e->char_albedo),  true  });
    s.raster_items = &e->raster_items;

    s.emissive     = e->emissive_enabled    ? &e->emissive_tris : nullptr;
    s.point_lights = e->point_light_enabled ? &e->point_lights  : nullptr;
    s.skybox = &e->skybox_faces;  s.skybox_enabled = e->skybox_enabled;  s.bg_color = e->bg_color;
    s.sun_enabled = e->sun_enabled;  s.reflections = e->reflections;  s.max_bounces = e->max_bounces;
    s.gi_enabled = e->gi_enabled;  s.gi_samples = e->gi_samples;  s.gi_strength = e->gi_strength;
    return s;
}
```

Se llama en `game_render` (sr_game.cpp:289) justo antes de `renderer.render`.

---

## 3. Cómo se agregan meshes y objetos

**No hay `scene.add(mesh)` ni un scene-graph.** Hay dos caminos, uno por backend:

### 3a. Para los ray tracers → triángulos en un `bvh::BVH`

El juego mantiene `std::vector<bvh::Tri> rt_tris` (miembro de `Game`). Cada frame:

```cpp
static void fold_mesh(std::vector<bvh::Tri>& out, const mesh& m,
                      const vec3& albedo, float roughness) {
    mat4 model = m.modelMatrix();                       // T·R·S de la mesh
    for (const triangle& tri : m.faces) {
        vec3 v0 = vec3(model * m.vertices[tri.v0].p);   // a mundo
        vec3 v1 = vec3(model * m.vertices[tri.v1].p);
        vec3 v2 = vec3(model * m.vertices[tri.v2].p);
        vec3 n  = normalize(cross(v1-v0, v2-v0));       // normal por winding (CCW = front)
        bvh::Tri t{ v0, v1, v2, n, albedo, roughness };
        if (m.tex) { t.tex = m.tex; t.uv0 = m.vertices[tri.v0].t; /* ... */ }
        out.push_back(t);
    }
}

static void build_dynamic(Game* e) {
    e->rt_tris.clear();
    if (e->character) fold_mesh(e->rt_tris, *e->character, e->char_albedo, e->char_rough);
    // ← PingPong RT: aquí añadir fold_mesh(ball), fold_mesh(racket), etc.
    e->dynamic_bvh.build(e->rt_tris, e->dynamic_build_strategy);   // rebuild COMPLETO, Morton
}
```

Geometría estática: `game_rebuild_static` (sr_game.cpp:105) hace lo mismo una vez
con `add_box(...)` → `static_bvh.build(tris, SAH)` → `renderer.reload_scene(...)`.

### 3b. Para el rasterizador → `RasterItem` con puntero a `mesh`

Ver `make_render_scene` arriba: lista de `{mesh*, color, shadow}`. El rasterizador
aplica `mesh->modelMatrix()` él mismo (no se pre-transforma).

**Consecuencia para *PingPong RT*:** cada objeto del juego (Ball, Racket, Player)
mantiene **una `mesh`** (para el raster) y **su transform**; el juego lo folda a
`rt_tris` para los tracers. Un objeto puede tener solo tris (esfera generada con
`add_sphere`) si no se necesita en el raster, o solo `mesh` — pero para que se vea
en los 4 backends conviene tener las dos representaciones, como hace hoy el suelo
(`field_mesh` + tris en `static_bvh`) y el personaje (`character` mesh + fold).

---

## 4. Transformaciones y materiales

### Transform
- `struct mesh` tiene `position`, `rotation` (euler rad), `scale` → `modelMatrix()`
  = `T · R · S` (cacheada, `_modelMatrixDirty`). Setters `setPosition/Rotation/Scale`,
  `updateRotation(delta)`.
- `SkinnedMesh::apply(mesh&, t)` sobrescribe `mesh.vertices[i].p` con la pose
  animada (deja el `modelMatrix` como transform global adicional).

### Material
- **Vive en el triángulo** (`bvh::Tri`): `albedo` (vec3 [0,1]), `roughness` [0,1]
  (0 = espejo, 1 = mate), `metallic` [0,1], `ior`, `emission` (vec3 HDR), `tex`
  (puntero, null = usar `albedo`), `smooth` + `n0/n1/n2` (normales por vértice).
- El rasterizador usa `renderConfig { baseColor, tex, lightInfluence, ignoreDepth,
  ignoreLight, backfaceCull }` — no PBR, solo color/textura plana.
- **No hay librería de materiales.** Se asigna a mano al foldear (`fold_mesh` pasa
  `albedo, roughness`; `add_sphere`/`load_obj_tris` los ponen por triángulo).

---

## 5. Cómo se pasa la cámara

Por puntero dentro de `RenderScene` (`s.cam = &e->cam`). El backend lee:
- **Raster:** `render_mesh(fb, *s.cam, mesh, cfg)` — usa `cam.view()` y `cam.projection()`.
- **CPU/Embree:** `get_ray_direction(*s.cam, x, y, w, h)` (sr_raytrace.cpp:16):
  ```cpp
  ndcX = (2*(px+0.5))/w - 1;  ndcY = 1 - (2*(py+0.5))/h;
  b = tan(radians(cam._fov)/2);  a = b / cam._aspectRatio;     // fov HORIZONTAL
  dirCam = (ndcX*b, ndcY*a, -1);
  return normalize( cam.rotation() * vec4(dirCam, 0) );
  origen del rayo = cam._position
  ```
- **OpenCL** (renderer.cpp:154): host precomputa la base:
  ```cpp
  mat4 R = s.cam->rotation();
  cx = R*(1,0,0);  cy = R*(0,1,0);  cz = R*(0,0,1);
  tb = tan(radians(cam._fov)/2);  ta = tb / cam._aspectRatio;
  ocl::render(cam._position, cx, cy, cz, tb, ta, SUN_DIR, ...)
  ```

**Para *PingPong RT*:** basta escribir `e->cam.setPosition(...)` / `setRotation(...)`
o `e->cam.lookAt(target)` en `game_update`. Ningún backend necesita cambios.

---

## 6. Selección de backend y raster↔ray tracing

`class Renderer` (renderer.hpp):

```cpp
void init(int num_workers, int max_bounces, float ambient, float shadow_eps);
void shutdown();
void upload_static(const bvh::BVH&, const array<texture,6>&, const vector<Tri>& emissive);
void reload_scene (...);                  // = on_scene_changed() + upload_static() a TODOS los backends
void render(RenderScene&, framebuffer&);  // → backends_[cur_]->render(...)
int  workers() const;
int  count() const;  int index() const;
const char* current_name() const;  bool current_available() const;
void cycle(int dir);                      // salta al siguiente backend DISPONIBLE (envuelve)
bool select(int i);                       // directo (tooling); false si fuera de rango o no disponible
```

- **`init` crea los backends en este orden**: `RasterBackend`(0),
  `CpuBvhBackend`(1), `EmbreeBackend`(2, si `WITH_EMBREE`), `OpenClBackend`(último).
  `cur_ = 1` → arranca en el ray tracer CPU, **no** en el raster.
- `game_handle_events`: `SDLK_TAB → renderer.cycle(-1)`; `SDLK_g → renderer.cycle(+1)`.
  **No hay un toggle raster/raytrace dedicado**: el raster es "un backend más"
  (índice 0), se llega a él ciclando.
- `RasterBackend::available()` siempre `true`. `OpenClBackend::available()` =
  `ocl::available()`. `EmbreeBackend::available()` siempre `true` (si compilado).

---

## 7. Ray tracer CPU — cómo funciona

```
CpuTracer::start(N)      → crea N hilos SDL parados en start_[i] (semáforos)
CpuTracer::render(scene, fb):
   job_scene_ = &scene; job_fb_ = &fb;
   dispatch(): SDL_SemPost(start_[i]) ×N;  SDL_SemWait(done_) ×N   ← bloquea hasta frame completo
worker i:  run_stripe(i) → trace_stripe(y0, y1)  con  y0 = (h/N)*i,  y1 = ... (último toma el resto)
trace_stripe: por cada (x,y):  ray r(cam.pos, get_ray_direction(...));
              fb.colorBuffer[y*w+x] = pack(trace_ray(r, scene, 0)) | 0xFF000000;
```

`trace_ray(ray, RenderScene, depth)` (sr_raytrace.cpp:103):
1. `scene.accel->intersect(o, d, hit)` — si miss → skybox o `bg_color`.
2. Si `tr.emission > 0` → devuelve `emission` (la luz es su propio color).
3. Normal: `N_geom` (cara, orientada al rayo), `N` (suave si `tr.smooth`), se
   voltea si `dot(N, dir) > 0`.
4. Albedo: `tr.tex` (UV interpoladas) o `tr.albedo`.
5. **Directa:** ambiente + sol (1 shadow ray) + cada emissive (punto en centroide,
   término geométrico, shadow ray) + cada `PointLight` (caída 1/d², shadow ray).
6. **GI** (solo `depth==0`, `k_d>0`, `metallic<0.5`): `gi_samples` rayos coseno-
   Hammersley rotados por `hash_pos(P)`, cada uno recoge la **directa** de lo que
   golpea (llamada recursiva con `depth = 1<<20` → terminal).
7. **Reflexión:** Fresnel-Schlick → `spec = fres*(1-roughness)`; si
   `depth < max_bounces && spec > 0.01 && reflections` → rayo espejo recursivo.
8. Combina: `local*(1-spec) + refl*spec*tint`.

`ISceneAccel` (accel.hpp) tiene 2 métodos: `intersect(o,d,SceneHit&)` y
`occluded(o,d,max_t)`. `BvhAccel` (bvh_accel.cpp): dynamic BVH (o brute_tris) +
static BVH, `best` compartido para quedarse con el más cercano.

---

## 8. OpenCL — cómo funciona (sr_ocl.cpp)

- `ocl::init(max_depth, ambient, shadow_eps)`: elige `CL_DEVICE_TYPE_GPU` (o
  `_ALL`), crea contexto/cola, **compila `KERNEL_SRC`** (string en el `.cpp`,
  `R"CLC(...)"`, ~180 líneas de OpenCL C que re-implementan traversal + shading),
  crea el kernel `trace`. Devuelve `false` si no hay dispositivo → el juego cae al
  CPU pool.
- `set_room(nb, nl, tf, nnodes, ntris)`: sube el BVH **estático** flatten-eado (1 vez).
- `set_sky(pixels, npx, off, w, h)`: sube el cubemap (1 vez).
- `set_dynamic(...)`: sube el BVH **dinámico** (cada frame).
- `set_emissive(tris, count)`: sube triángulos-luz (layout 32 floats/tri).
- `render(cam_pos, cx, cy, cz, tb, ta, sun, spp, skybox, reflections, W, H, out)`:
  1 work-item/píxel, escribe `out` (W·H ARGB), `spp` fijo a 8 desde `renderer.cpp`.
- `device_name()` → p. ej. `"NVIDIA GeForce RTX 3050 Laptop GPU"`.

**Layout de `BVH::flatten` (contrato host↔kernel, bvh.cpp:415):**
- `node_bounds`: 8 floats/nodo (min.xyz+pad, max.xyz+pad).
- `node_links`: 4 ints/nodo (`left, right, start, count`; `left < 0` = hoja).
- `tris`: 32 floats/tri (`v0,v1,v2, normal, albedo, roughness, metallic, ior,
  smooth, pad, n0,n1,n2, emission`).

---

## 9. Embree — cómo funciona (`#ifdef WITH_EMBREE`)

- `embree_ref::Scene` (embree_bvh.hpp): `build(vector<Tri>, strategy)` copia los
  vértices a un buffer, crea `RTC_GEOMETRY_TYPE_TRIANGLE`, `rtcCommitScene` (=
  construye el BVH de Embree). `SAH→HIGH, Median→MEDIUM, Morton→LOW`.
  `intersect(o,d,t,prim)` / `occluded(o,d,max_t)`.
- `EmbreeAccel : ISceneAccel` (embree_accel.hpp): 2 `Scene` (static + dynamic).
  `sync()` reconstruye el estático solo si `static_dirty_`, el dinámico siempre.
  `intersect` combina ambos por distancia y devuelve el `bvh::Tri*` original (para
  que el shading sea idéntico al del BVH propio).

---

## 10. BVH y objetos dinámicos — ver [BVH_GUIDE.md]

Resumen del contrato para *PingPong RT*:
- **Estático** (`static_bvh`): construir en `game_rebuild_static` (suelo, pared,
  mesa, red). Rebuild solo al cambiar la escena o la estrategia.
- **Dinámico** (`dynamic_bvh`): `dynamic_bvh.build(rt_tris, Morton)` **cada frame**
  tras foldear pelota + raqueta + jugador. No hay refit/update incremental.
- `use_bvh = false` → salta el dinámico y hace fuerza bruta O(N) sobre `brute_tris`
  (solo debug/comparación).

---

## 11. Qué interfaz hay que CONSERVAR (checklist para el agente de Integración)

1. `RenderScene` sigue siendo POD de punteros prestados; el juego lo rellena en
   `make_render_scene`.
2. `render/` no incluye `game/`. Añadir un campo a `RenderScene` es aceptable;
   pasar un `Game*` **no**.
3. `Renderer::render(RenderScene&, framebuffer&)` es el único punto de dispatch.
4. El juego construye los BVH y pasa punteros; el renderer no los posee ni
   reconstruye.
5. El shading vive en `trace_ray` **y** en `KERNEL_SRC` — mantener sincronía o
   documentar la divergencia.
6. `trace_ray` solo consulta visibilidad por `scene.accel` (nunca un BVH concreto).
7. El render corre en N hilos y solo lee `RenderScene`; el juego no muta escena
   durante `renderer.render`.
