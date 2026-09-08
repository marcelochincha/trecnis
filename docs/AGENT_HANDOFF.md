# AGENT_HANDOFF.md — reparto de trabajo entre agentes de IA

> El desarrollo de `pingpong-rt/` se hará con varios agentes. Este documento define
> el alcance de cada uno: qué puede tocar, qué **no**, qué interfaces debe
> respetar, sus dependencias y su criterio de aceptación.
>
> **Reglas globales para TODOS los agentes:**
> 1. C++17. No cambiar el estándar.
> 2. **`src/render/raytrace/bvh.{hpp,cpp}` es INTOCABLE.** Se usa como caja negra.
> 3. `src/render/` no puede incluir nada de `src/game/`. El único puente es
>    `RenderScene` (POD en `render/render_scene.hpp`).
> 4. `src/game/` no implementa ray tracing ni incluye `sr_ocl.hpp`/`cpu_tracer.hpp`
>    /`sr_raytrace.hpp`. Solo `render/renderer.hpp`, `render/render_scene.hpp` y
>    `render/raytrace/bvh.hpp` (para construir árboles).
> 5. Un cambio de shading en `render/raytrace/sr_raytrace.cpp` obliga a replicarlo
>    en `render/raytrace/sr_ocl.cpp` (`KERNEL_SRC`) **o** a documentar la divergencia.
> 6. Orden del frame inviolable: `update` completo → construir BVH → `render`.
>    Nada muta el estado de escena durante `renderer.render`.
> 7. No borrar código. No refactors grandes. Marcar lo no implementado.
> 8. Cada PR compila con `-DWITH_EMBREE=OFF -DWITH_OPENCL=ON` y corre sin crash.
> 9. Leer [ARCHITECTURE.md], [RENDERER_API.md], [BVH_GUIDE.md] antes de empezar.

---

## Mapa de propiedad de archivos

| Zona | Dueño principal | Co-editan |
|---|---|---|
| `core/ math/(salvo sr_aabb) render/ io/` | **nadie** (copiado de trecnis, congelado) | solo Agente 8 con justificación |
| `math/sr_aabb.hpp` | Agente 5 (Collision) | — |
| `engine/anim/` | Agente 6 (Animation) | Agente 2 (Player) lee |
| `engine/assets/obj_loader` | Agente 6 | Agente 2, 3 leen |
| `engine/geom/shapes` | Agente 1 (crea) | Agentes 2,3,4 usan |
| `game/game.cpp game_state.hpp sr_game.hpp` | **Agente 8 (Integration)** | todos proponen diffs, Agente 8 los aplica |
| `game/court.*` | Agente 1 | — |
| `game/player.*` | Agente 2 | — |
| `game/racket.*` | Agente 3 | — |
| `game/ball.*` | Agente 4 | — |
| `game/physics.*` | Agente 4 | — |
| `game/collision.*` | Agente 5 | Agente 4 lee |
| `game/game_camera.*` | Agente 7 | — |
| `game/hud.*` | Agente 8 | Agente 9 añade métricas |
| `tests/` | Agente 9 | Agentes 4,5 aportan casos |
| `CMakeLists.txt` | Agente 8 | — |

> **`game/game.cpp` es el punto caliente**: contiene el game loop, la construcción
> de escena y `make_render_scene`. Para evitar conflictos, **solo el Agente 8 lo
> edita**; el resto entrega su módulo con una interfaz clara (`Xxx::update(dt)`,
> `Xxx::fold_into(rt_tris)`, …) y un snippet de "cómo cablearlo", y el Agente 8 lo
> integra.

---

## AGENTE 1 — Arquitectura / Infraestructura / Cancha

**Objetivo:** montar `pingpong-rt/` copiando el motor de trecnis, dejarlo
compilando y arrancando, y construir la cancha estática (suelo + pared).

**Puede modificar / crear:**
- Toda la estructura de carpetas y el copiado de `core/ math/ render/ io/ sound/
  engine/anim/ engine/assets/ main.cpp sr_config.hpp` desde trecnis.
- `CMakeLists.txt` (adaptado de trecnis — ver [BUILD_GUIDE.md §4]).
- `src/engine/geom/shapes.{hpp,cpp}` — NUEVO: portar `add_box`, `add_box_c`,
  `add_sphere` de `raytrec/src/game/sr_scene.cpp` (líneas 37-83).
- `src/game/court.{hpp,cpp}` — NUEVO: `class Court` que genera suelo + pared como
  `std::vector<bvh::Tri>` y expone `build(static_bvh&, build_strategy)`.
- `third_party/ lib/ bin/ assets/` (copiar de trecnis).
- `README.md`.

**NO puede modificar:** nada de `render/` salvo copiarlo; `bvh.*`; los módulos de
los demás agentes.

**Interfaces que debe respetar / definir:**
- `Court::build(bvh::BVH& static_bvh, bvh::BuildStrategy s)` y
  `Court::floor_plane_y()`, `Court::wall_plane_x()` (constantes que Collision usará).
- Mantener la API pública de `game/sr_game.hpp` (`game_create/init/update/
  handle_events/render/shutdown` + `game_rebuild_static`).
- `RenderScene` sin cambios en esta fase.

**Dependencias:** [BUILD_GUIDE.md], [ARCHITECTURE.md], [RENDERER_API.md].

**Criterios de aceptación:**
1. `cmake --build build -j4` OK con `-DWITH_EMBREE=OFF -DWITH_OPENCL=ON`.
2. La ventana abre; se ve skybox + suelo + pared.
3. `[TAB]`/`[G]` ciclan Raster / CPU SOFTWARE / OCL GPU sin crash y muestran la
   misma cancha.
4. `[ESC]` cierra limpio; sin fugas obvias (`game_shutdown` libera).
5. HUD muestra FPS, backend, nº de tris del static BVH.

---

## AGENTE 2 — Player

**Objetivo:** jugador 3D visible y controlable (movimiento), sin física de
colisión (clamp de posición).

**Puede modificar / crear:**
- `src/game/player.{hpp,cpp}` — NUEVO: `class Player { vec3 pos; float facing;
  mesh mesh_; SkinnedMesh skin_; void read_input(const Uint8* keys, int mdx, int
  mdy, float yaw); void update(float dt); void fold_into(std::vector<bvh::Tri>&
  out); const mesh* raster_mesh() const; }`.
- Casos de uso de `build_procedural_character` (fase 3) y, en fase 9,
  `load_obj_mesh` + `SkinnedMesh::load`.

**NO puede modificar:** `engine/anim/skinned_mesh.*` (es del Agente 6; si necesita
un cambio, lo pide); `render/`; `game/game.cpp` (entrega snippet de cableado);
`court/racket/ball/collision`.

**Interfaces que debe respetar:**
- `Player::fold_into(rt_tris)` produce `bvh::Tri` en espacio mundo (patrón
  `fold_mesh` de `sr_game.cpp:81`).
- `Player::update(dt)` NO llama a `dynamic_bvh.build` (eso lo hace game.cpp).
- Movimiento en plano XZ, sin Y libre; respeta límites que expone `Court`.
- Usa `math/` y `core/sr_geometry.hpp` (mesh), nada de `render/raytrace/` salvo
  `bvh.hpp` (para el tipo `Tri`).

**Dependencias:** Agente 1 (Court, shapes), Agente 6 (SkinnedMesh estable),
[CODE_INVENTORY.md "personaje actual"], [MIGRATION_PLAN.md §1].

**Criterios de aceptación:**
1. El jugador se ve en los 3 backends, animación idle corriendo.
2. WASD lo mueven relativo a `yaw`; no sale de la cancha ni atraviesa la pared
   (clamp).
3. `dynamic_bvh` build time en HUD < 2 ms con el jugador dentro.
4. Sin cambios en `render/` ni en `RenderScene`.

---

## AGENTE 3 — Racket

**Objetivo:** raqueta unida al jugador, con animación de swing disparada por
click, **sin** efecto físico sobre la pelota (eso es del Agente 5 + 8 en fase 8).

**Puede modificar / crear:**
- `src/game/racket.{hpp,cpp}` — NUEVO: `class Racket { enum State {Idle, Swing,
  Cooldown}; mesh mesh_; void attach(const Player&); void update(float dt); void
  swing(); void fold_into(std::vector<bvh::Tri>&); AABB world_aabb() const; State
  state() const; vec3 face_normal() const; }`.

**NO puede modificar:** `player.*` (lee la interfaz pública), `render/`,
`game/game.cpp` (snippet), `bvh.*`.

**Interfaces que debe respetar:**
- El transform de la raqueta = `player_transform · local_offset · swing_rotation`.
  Componer con `math/sr_lingalg.hpp` (`mat4`, `translationMatrix`, `rotationMatrix`).
- `Racket::world_aabb()` en coordenadas mundo — lo consumirá `Collision` (Agente 5).
- `swing()` es idempotente si ya está en `Swing`.
- Animación = lerp de `mesh.rotation` (no skinning).

**Dependencias:** Agente 2 (Player), Agente 8 (añade el `case
SDL_MOUSEBUTTONDOWN` en `game_handle_events` y llama `racket.swing()`).

**Criterios de aceptación:**
1. La raqueta sigue al jugador en todos los frames.
2. Click izq → animación de swing visible → vuelve a Idle tras el cooldown.
3. `world_aabb()` encierra correctamente la raqueta (verificable con overlay `[V]`
   o un test).
4. Sin tocar `render/`.

---

## AGENTE 4 — Ball Physics

**Objetivo:** pelota con integrador de física (gravedad + drag lineal). **Sin
spin, sin Magnus** (fase 12). Sin colisión (Agente 5).

**Puede modificar / crear:**
- `src/game/ball.{hpp,cpp}` — NUEVO: `class Ball { vec3 pos, vel; float radius;
  void update(float dt); void fold_into(std::vector<bvh::Tri>&); const mesh*
  raster_mesh() const; void reset(vec3 p, vec3 v); }`.
- `src/game/physics.{hpp,cpp}` — NUEVO: `namespace physics { struct Params { float
  g, dt, drag; }; void integrate(vec3& pos, vec3& vel, const Params&, float dt); }`.
- `tests/test_physics.cpp`.

**NO puede modificar:** `collision.*` (del Agente 5), `render/`, `game/game.cpp`
(snippet), `bvh.*`.

**Interfaces que debe respetar:**
- Euler **semi-implícito**: `vel += a·dt; pos += vel·dt` (igual que
  `RTT_Shooting_Method.ipynb §3` y `ball-physics-explained.md §1`).
- Constantes de referencia: `G ≈ 9.8`, `DT = 1/60`, `DRAG_COEF ≈ 0.06`.
- `Ball::update` NO resuelve colisiones (llama solo a `physics::integrate`).
- Geometría de la pelota: `add_sphere` (esfera UV baja, `slices≈12 stacks≈8`,
  `smooth=true`). Decidir: regenerar tris cada frame vs 1 mesh fija +
  `setPosition` (documentar la elección en el `.hpp`).

**Dependencias:** Agente 1 (`shapes.add_sphere`), `RTT_Shooting_Method.ipynb`,
`ball-physics-explained.md`.

**Criterios de aceptación:**
1. Una pelota con `vel` inicial describe una parábola estable a 60 fps.
2. `test_physics.cpp`: la posición tras N pasos coincide (±ε) con un cálculo de
   referencia.
3. Estable con `dt` de hasta 1/30 (sub-step si hace falta).
4. La pelota se ve redonda en los 3 backends.

---

## AGENTE 5 — Collision

**Objetivo:** detección + respuesta de colisiones para mantener el peloteo:
pelota ↔ suelo, pelota ↔ pared, pelota ↔ raqueta.

**Puede modificar / crear:**
- `src/game/collision.{hpp,cpp}` — NUEVO: `namespace collision { struct Contact {
  bool hit; vec3 normal; float penetration; }; Contact sphere_plane(vec3 center,
  float r, vec3 plane_n, float plane_d); Contact sphere_aabb(vec3 center, float r,
  const AABB&); void resolve(Ball&, const Contact&, float restitution); }`.
- `src/math/sr_aabb.hpp` — NUEVO: helpers `aabb_from_points`, `aabb_contains`,
  `aabb_closest_point`, `aabb_overlap` (reimplementar; **no** sacar de `bvh.cpp`).
- `tests/test_collision.cpp`.

**NO puede modificar:** `bvh.*` (puede *llamar* a `static_bvh->intersect` para
look-ahead), `physics.*` (del Agente 4), `render/`, `game/game.cpp` (snippet).

**Interfaces que debe respetar:**
- Rebote: `v' = v - (1+e)·(v·n)·n` con `e` = restitución (MVP: `e ≈ 0.9` suelo,
  `e ≈ 0.95` pared — valores del `.md`).
- Consumir `Court::floor_plane_y()`, `Court::wall_plane_x()`, `Racket::world_aabb()`,
  `Racket::state()`, `Racket::face_normal()`.
- Para MVP: **planos analíticos**, no el BVH. El BVH solo si aparece geometría
  irregular (documentar).
- Resolver penetración (empujar la pelota fuera) además de reflejar la velocidad.

**Dependencias:** Agentes 1, 3, 4; `ball-physics-explained.md §2` (modelo de
rebote), `RTT_Shooting_Method.ipynb`.

**Criterios de aceptación:**
1. La pelota rebota indefinidamente entre pared y devoluciones, sin atravesar el
   suelo ni la pared.
2. Ángulo de incidencia ≈ ángulo de reflexión (test).
3. Sin tunneling a la velocidad máxima esperada del juego (test con `dt` grande).
4. En fase 8: cuando `racket.state()==Swing` y la AABB interseca la esfera, la
   pelota sale hacia la pared.

---

## AGENTE 6 — Animation

**Objetivo:** infraestructura de animación estable y cableada: skinning del
jugador (fase 9) y animaciones simples (swing, idle).

**Puede modificar / crear:**
- `src/engine/anim/skinned_mesh.{hpp,cpp}` — copiado de trecnis; **puede
  ampliar** (p. ej. blending entre clips), no romper la API existente
  (`load`, `apply`, `build_procedural_character`).
- `src/engine/anim/camera_anim.{hpp,cpp}` — copiado; disponible para el Agente 7.
- `src/engine/assets/obj_loader.{hpp,cpp}` — copiado + **cableado** (hoy es código
  muerto en trecnis); exponer `load_obj_mesh`/`load_obj_tris` para Agentes 2 y 8.
- Assets de animación bajo `assets/` (o `res/`).

**NO puede modificar:** `render/`, `bvh.*`, `game/*` (entrega la infra; Agentes 2/3
la consumen).

**Interfaces que debe respetar:**
- `SkinnedMesh::apply(mesh&, float t_seconds)` deja `mesh` dirty y en espacio
  local (el `modelMatrix` de la mesh lo lleva a mundo).
- `load_obj_mesh` llena `mesh.src_vertex` (necesario para el binding por índice).
- Formato de ficheros de rig: el documentado en la cabecera de
  `skinned_mesh.hpp` (binding: `bones`/`verts` + V ints; anim: `fps`/`bones`/
  `frames` + F·B matrices column-major).

**Dependencias:** [CODE_INVENTORY.md] (sección personaje actual), patrón de
`raytrec/src/game/sr_scene.cpp:549 build_character`.

**Criterios de aceptación:**
1. `build_procedural_character` sigue funcionando idéntico.
2. `load_obj_mesh("...robloxian.obj")` + `SkinnedMesh::load` reproduce una
   animación (probado con los assets de raytrec).
3. `obj_loader` cableado y usado por al menos un módulo.
4. Sin regresión en el resto (compila y corre).

---

## AGENTE 7 — Camera

**Objetivo:** cámara de juego (seguimiento del jugador / fija tipo RTT),
sustituyendo el free-fly.

**Puede modificar / crear:**
- `src/game/game_camera.{hpp,cpp}` — NUEVO: `class GameCamera { void update(float
  dt, const Player&, const Ball&); void apply(camera& cam); }` (posición detrás/
  encima del jugador, mira a la pared o a la pelota, smoothing con lerp).
- Puede usar `engine/anim/camera_anim` para una intro / repetición.

**NO puede modificar:** `core/sr_camera.{hpp,cpp}` (la clase `camera` **no cambia**
— solo cambia quién la maneja); `render/`; `player/ball`.

**Interfaces que debe respetar:**
- Solo escribe `cam.setPosition(...)`, `cam.setRotation(...)` / `cam.lookAt(...)`,
  `cam.setFov(...)`.
- Convención: forward = -Z, +Y arriba, RH. `euler_deg_looking_at` de
  `camera_anim.cpp` da el euler correcto para mirar a un punto.
- Recalcular `cam._aspectRatio` si la ventana cambia de tamaño (hoy no se hace —
  coordinar con Agente 8 si se implementa resize).

**Dependencias:** Agentes 2, 4; [MIGRATION_PLAN.md §2].

**Criterios de aceptación:**
1. La cámara sigue al jugador de forma suave, sin mareo.
2. La pelota y la pared quedan siempre en encuadre durante un peloteo.
3. `camera` (clase) sin cambios.

---

## AGENTE 8 — Integration

**Objetivo:** dueño de `game/game.cpp`, `game_state.hpp`, `sr_game.hpp`,
`hud.*`, `CMakeLists.txt`. Integra los módulos de los demás agentes en el game
loop y la escena.

**Puede modificar:**
- `src/game/game.cpp` — game loop callbacks, construcción de escena,
  `make_render_scene`, input dispatch (`case SDL_MOUSEBUTTONDOWN`), orden de
  `game_update` (ver [MIGRATION_PLAN.md §4]).
- `src/game/game_state.hpp` — añadir `Player player; Racket racket; Ball ball;
  Court court; GameCamera game_cam;` a `struct Game`.
- `src/game/sr_game.hpp` — mantener la API pública; añadir solo si es imprescindible.
- `src/game/hud.{hpp,cpp}` — menú + marcador.
- `CMakeLists.txt`, `main.cpp` (título de ventana), `README.md`.
- **`render/render_scene.hpp` SOLO** si un agente justifica un dato nuevo para el
  shading (con revisión).

**NO puede modificar:** `player/racket/ball/physics/collision/game_camera/
skinned_mesh` (son de sus dueños; el Agente 8 consume sus interfaces públicas);
`bvh.*`; el resto de `render/`.

**Interfaces que debe respetar:**
- Orden del frame: input → `player.update` → `racket.update` → `ball.update` →
  `collision.resolve` → `game_cam.update` → **build BVH** (`rt_tris.clear()`; fold
  ball/racket/player; `dynamic_bvh.build(rt_tris, Morton)`) → `render`.
- `make_render_scene` sigue devolviendo un `RenderScene` POD; `s.dynamic_bvh =
  &e->dynamic_bvh`, `s.static_bvh = &e->court_bvh`, etc.
- `render/` intacto.

**Dependencias:** todos los demás agentes; [RENDERER_API.md], [MIGRATION_PLAN.md].

**Criterios de aceptación:**
1. El MVP corre: jugador se mueve, raqueta golpea, pelota rebota en la pared y
   vuelve, el peloteo se puede mantener.
2. Los 3 backends (Raster / CPU BVH / OpenCL) renderizan la escena completa; CPU
   BVH y OpenCL coinciden.
3. `game/` no incluye `sr_ocl.hpp` / `cpu_tracer.hpp` / `sr_raytrace.hpp`.
4. HUD con FPS, backend, marcador, build times de los BVH.
5. `[ESC]` cierra limpio.

---

## AGENTE 9 — Testing / Optimization

**Objetivo:** suite de tests (física, colisión, math) y análisis de rendimiento;
optimización del BVH dinámico **solo si** los datos lo justifican.

**Puede modificar / crear:**
- `tests/` — NUEVO: `test_physics.cpp`, `test_collision.cpp`, `test_math.cpp`,
  runner mínimo (asserts, sin framework externo).
- `CMakeLists.txt` — añadir target `tests` (coordinar con Agente 8).
- Métricas en `hud.*` (coordinar con Agente 8): build ms de static/dynamic BVH,
  nodos/rayo, tris.
- Fase 11: pool de `rt_tris` reutilizado, o TLAS por objeto — **sin reemplazar
  `bvh::BVH`**, documentando cada cambio.

**NO puede modificar:** `bvh.{hpp,cpp}` (medir y envolver, no reescribir);
lógica de gameplay de los demás agentes (solo tests).

**Interfaces que debe respetar:**
- Los tests no dependen de SDL ni de una ventana (compilan `game/physics.cpp`,
  `game/collision.cpp`, `math/` de forma aislada — coordinar que esos `.cpp` no
  arrastren SDL).
- Cualquier optimización mantiene el layout de `BVH::flatten` (contrato con la GPU).

**Dependencias:** Agentes 4, 5, 8; [BVH_GUIDE.md].

**Criterios de aceptación:**
1. `cmake --build build --target tests && ./tests` pasa.
2. Cobertura mínima: integrador de física, rebote esfera-plano, esfera-AABB,
   operaciones de `mat4`/`vec3`.
3. Informe de rendimiento: frame time por backend a 640×360 y 1280×720, build ms
   de los BVH, con y sin GI/reflejos.
4. Si se optimiza el BVH dinámico: justificación con números + los 3 backends
   siguen coincidiendo.

---

## Orden de arranque sugerido

```
Agente 1  ─────────────────────────────►  (FASE 0-2)  base + cancha
                    │
        ┌───────────┼───────────┬───────────┐
        ▼           ▼           ▼           ▼
   Agente 6     Agente 2     Agente 4     Agente 7
   (anim infra) (Player)     (Ball+Phys)  (Camera)
        │           │           │           │
        └─────┬─────┘           ▼           │
              ▼             Agente 5        │
          Agente 3         (Collision)      │
          (Racket)             │            │
              └────────┬───────┘            │
                       ▼                    │
                  Agente 8  ◄───────────────┘   (Integration, continuo)
                       │
                       ▼
                  Agente 9  (Testing/Opt, continuo desde FASE 6)
```

Agentes 2, 4, 6, 7 pueden trabajar en paralelo tras la Fase 2 del Agente 1.
Agente 8 integra de forma incremental. Agente 9 arranca cuando existe `physics.*`.
