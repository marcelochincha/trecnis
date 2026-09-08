# MIGRATION_PLAN.md — de `trecnis` a `pingpong-rt`

> Plan para **crear** el nuevo proyecto reutilizando `trecnis` (base) + piezas de
> `raytrec`. **No** contiene código de juego. Cada fase tiene archivos, objetivo,
> dependencias, criterio de "hecho" y riesgos.

---

## 1. Personaje actual (análisis para reutilizar como Jugador + Raqueta)

**Verificado en `trecnis/src/engine/anim/skinned_mesh.cpp` + `sr_game.cpp`.**

| Pregunta | Respuesta (código real) |
|---|---|
| ¿Procedural o mesh? | **Procedural.** `build_procedural_character(mesh&, SkinnedMesh&)` genera la geometría en código. No carga ningún fichero. |
| Forma | Columna de anillos apilados: `bones=7`, `seg=0.34`, `ringsPerSeg=4`, `slices=14`, radio `0.30→0.10`, altura `2.38`. ~406 vértices, ~812 tris + tapa. |
| Nº de huesos | 7 (cadena lineal, sin jerarquía real: se compone acumulando `A = A · T(P-prevP) · R(θ)`). |
| Sistema de animación | **Baked, sin runtime rig.** 60 frames, 24 fps, loop 2.5 s. Cada frame·hueso guarda una `mat4` de skinning ya compuesta: `θ_b = (0.22+0.03b)·sin(ph - 0.9b)` (onda viajera hacia la punta). |
| Skinning | **Rígido, 1 hueso por vértice** (`vertexBone[v]`), sin pesos. `p' = M[frame][bone[v]] · restP(v)`. Interpolación lineal entre los 2 frames que rodean `t`. |
| Transform | La `mesh` tiene además `position/rotation/scale` → `modelMatrix()` global. `skin.apply` escribe `mesh.vertices[i].p` en espacio local; el `modelMatrix` lo lleva a mundo. |
| Cámara asociada | Ninguna en trecnis. (raytrec tiene escena "Character" con path de cámara; trecnis la quitó.) |
| Cómo se actualiza | `game_update`: `anim_time += dt`; loop; `skin.apply(*character, anim_time)`; luego `build_dynamic` folda la mesh deformada al BVH dinámico. |
| Cómo entra al RenderScene | `make_render_scene` lo añade a `raster_items` (`{character, char_albedo, true}`). Para los tracers va vía `build_dynamic` → `fold_mesh` → `rt_tris` → `dynamic_bvh`. |
| Cómo entra al BVH dinámico | `fold_mesh(rt_tris, *character, char_albedo, char_rough)` cada frame, antes de `dynamic_bvh.build(rt_tris, Morton)`. |

### Qué reutilizar para Jugador + Raqueta

| Necesidad | Reutilizar | Adaptar |
|---|---|---|
| **Jugador (MVP)** | `build_procedural_character` tal cual como placeholder visible | envolver en una clase `Player` con `pos`, `facing`, y mover la `mesh` via `setPosition` según input |
| **Jugador (fase 9)** | `SkinnedMesh::load` + `load_obj_mesh` (patrón de `raytrec/sr_scene.cpp:549 build_character`) | assets propios o `robloxian.obj` + `skin.weights`/`skin.anim` |
| **Raqueta** | `add_box` (paleta = caja fina) **o** `create_cube` + `mesh.scale` | clase `Racket` con transform relativo a la mano/cámara del jugador; `swing` = animación corta de rotación (lerp de `mesh.rotation`) |
| **Skinning infra** | `SkinnedMesh` completo (struct + apply + load) | ninguna; es genérico |

**No modificar** `skinned_mesh.{hpp,cpp}`. Envolver en `game/`.

---

## 2. Cámara — adaptación a videojuego de tenis de mesa

**Verificado en `core/sr_camera.{hpp,cpp}` + uso en `sr_game.cpp:196,266-267`.**

| Aspecto | Estado actual | Adaptación para *PingPong RT* |
|---|---|---|
| Posición | `cam._position`; en trecnis la escribe el free-fly de `game_update` | escribir desde `Camera::update()` del juego: fija detrás/encima del jugador |
| Orientación | `cam._rotation` euler (pitch, yaw, roll) rad, orden ZYX | `cam.setRotation(vec3(pitch, yaw, 0))` o `cam.lookAt(ball_or_table_center)` |
| Matrices | `view()`/`projection()`/`rotation()` cacheadas, dirty-flag | sin cambios |
| Projection | perspectiva RH, `_fov` HORIZONTAL en grados, `_aspectRatio = w/h` | fijar `setFov(~70)`; recalcular aspect si la ventana cambia de tamaño (hoy no se hace) |
| Input | mouse relativo → `yaw/pitch` en `game_update:237-239` | mouse → apuntar la raqueta; la cámara puede seguir suavemente al jugador o ser fija |
| Movimiento | free-fly WASD + bob al caminar | **quitar el free-fly**; la cámara la posiciona el juego |
| Seguimiento | no existe | `Camera::follow(player.pos, offset)` con lerp/smoothing |
| Animación | `camera_anim.cpp` (`sample_camera_anim`) existe, no se usa | opcional fase 10: intro / repetición con `load_camera_anim` |
| Relación con jugador | ninguna | la cámara vive "detrás del jugador mirando a la pared" |

**La clase `camera` no necesita cambios.** Lo que cambia es **quién** escribe
`setPosition`/`setRotation`: hoy el bloque free-fly de `game_update`, mañana una
clase `GameCamera` en `game/`.

**Regla del engine (README trecnis):** forward = -Z en espacio de ojo, +Y arriba,
right-handed. `euler_deg_looking_at(pos, target)` (en `camera_anim.cpp`) ya calcula
el euler para mirar a un punto en la convención del juego (`yaw = atan2(d.x, -d.z)`,
`pitch = asin(d.y)`).

---

## 3. Input — mapa objetivo (sin implementar aún)

**Verificado: input en 2 sitios.**
- `game_update` (polling, continuo): `SDL_GetKeyboardState` (W/A/S/D, LSHIFT,
  SPACE), `SDL_GetRelativeMouseState(&mdx,&mdy)` (mirar).
- `game_handle_events` (eventos discretos): `SDL_QUIT`, `SDLK_ESCAPE`, `SDLK_m`,
  `SDL_MOUSEWHEEL`, y en modo no-menú: `TAB, B, V, H, N, L, G`.
- `SDL_SetRelativeMouseMode(SDL_TRUE)` en `game_init` (ratón capturado).

| Acción *PingPong RT* | Dónde va | Cómo |
|---|---|---|
| **WASD → mover jugador** | `game_update`, reemplaza el cálculo de `moveDir`/`e->position` | mismo patrón (`inputDir` rotado por `yaw`), pero mueve `player.pos` con límites de cancha, sin componente Y libre |
| **Mouse → apuntar / cámara** | `game_update`, `mdx/mdy` | acumular en un "aim yaw/pitch" de la raqueta o de la cámara-seguimiento |
| **Click izq → golpe de raqueta** | `game_handle_events`, **nuevo** `case SDL_MOUSEBUTTONDOWN` con `event.button.button == SDL_BUTTON_LEFT` | dispara `racket.swing()` (máquina de estados: idle→swinging→cooldown) |
| **ESC → salir** | ya existe (`SDLK_ESCAPE`) | sin cambios |
| Mantener (debug): `TAB/G` backend, `B` BVH, `V/N` overlays, `M` menú | ya existen | conservar para depurar |

**No** tocar `main.cpp` para esto: `game_handle_events` ya recibe todos los
`SDL_Event`.

---

## 4. Game loop — dónde entra el gameplay

**Verificado: `game_update` (sr_game.cpp:219) hace hoy, en orden:**
```
1. if (show_menu) return;
2. skinning:  anim_time += dt;  loop;  skin.apply(*character, anim_time)
3. build_dynamic(e)                          ← reconstruye el BVH dinámico
4. input:  keyboard → moveDir;  mouse → yaw/pitch
5. cámara:  e->position += moveDir*dt;  cam.setPosition/setRotation
6. time += dt;  orbita la point light
```

**`game_render` (sr_game.cpp:280):**
```
1. fb.clear(bg)
2. scene = make_render_scene(e)
3. renderer.render(scene, fb)                ← 1 punto de dispatch
4. render_gizmo;  if(show_bvh) draw_bvh_debug;  if(show_normals) draw_normals_debug
5. HUD + menú
6. SDL_UpdateTexture
```

### Orden objetivo de `game_update` en *PingPong RT*
```
if (paused/menu) return;

1. input:            player.read_input(keys, mouse_dx, mouse_dy)
2. player.update(dt)         → mueve player.pos (con límites de cancha)
3. racket.update(dt)         → sigue al jugador; avanza animación de swing
4. ball.update(dt)           → integra física (Euler semi-implícito; ver notebook)
5. collision.resolve(ball, {floor, wall, table}, racket)   → rebotes, cambia ball.vel
6. camera.update(dt, player) → posiciona cam detrás del jugador
7. (opcional) score/state machine: ¿punto perdido? ¿saque?
8. rebuild geometry:
     rebuild_static solo si algo estático cambió (raro)
     build_dynamic:  rt_tris.clear();
                     fold ball, racket, player  →  dynamic_bvh.build(rt_tris, Morton)
9. time += dt
```

**Puntos de inserción concretos** (líneas de `sr_game.cpp`):
- Física + colisiones + cámara del juego → **reemplazan** el bloque 4-5 actual
  (free-fly + orbit light) de `game_update`.
- `build_dynamic` (paso 3 actual) → **se mueve al final** (paso 8), tras mover los
  objetos, y folda más de un objeto.
- El skinning (paso 2 actual) se mantiene, dentro de `player.update`.

---

## 5. Física y colisiones existentes — inventario (qué hay para reutilizar)

**Búsqueda exhaustiva en `trecnis/src/`.** Resultado:

| Se buscó | ¿Existe? | Dónde | Reutilizable para |
|---|---|---|---|
| Vectores (dot, cross, normalize, lerp, length) | ✅ | `math/sr_vec.hpp` | todo |
| `reflect(d, n)` | ✅ | `sr_raytrace.cpp:80` `reflect_dir` (`d - n·2·dot(d,n)`) — **`static`**, copiar la fórmula | rebote pelota↔pared/mesa |
| `AABB` (struct) | ✅ | `sr_geometry.hpp:22` | bounding box de raqueta/jugador |
| AABB helpers (grow, union, area, slab-test) | ✅ pero **`static` en `bvh.cpp`** (no exportados) | `bvh.cpp:12-62` | copiar a un `math/sr_aabb.hpp` nuevo |
| Ray–triángulo (Möller–Trumbore) | ✅ | `bvh.cpp:65` `tri_hit` (static), `bvh_accel.cpp:5` `ray_intersect_triangle` (static) | look-ahead raycast |
| Ray–AABB | ✅ | `bvh.cpp:48` `aabb_hit` (static) | — |
| `BVH::intersect` / `occluded` (rayo↔escena) | ✅ público | `bvh.hpp` | raycast de la pelota contra geometría estática |
| Esfera–esfera / esfera–plano / esfera–AABB / esfera–cápsula | ❌ **no existe** | — | **crear** (`CollisionSystem`) |
| Tests barridos (swept) / CCD | ❌ | — | **crear** (o aproximar rayo+radio) |
| Integrador de física / resolución de contacto / impulsos | ❌ | — | **crear** (diseño ya está en el notebook y `ball-physics-explained.md`) |
| Colisión AABB–AABB | ❌ | — | **crear** (trivial) |
| Distancia punto–plano, punto–AABB | ❌ | — | **crear** (trivial) |
| Normales de superficie | ✅ | `bvh::Tri::normal` / `tri_smooth_normal` | usar la normal del tri golpeado |
| Transformaciones (mat4, T/R/S, euler) | ✅ | `math/sr_lingalg.hpp` | posicionar raqueta/jugador |

**Conclusión:** existe **toda la infraestructura de rayos y vectores**, y **nada de
física de sólidos ni colisiones de volúmenes**. `Ball`, `Physics` y
`CollisionSystem` se escriben desde cero (fases 5-7), guiados por
`RTT_Shooting_Method.ipynb` y `ball-physics-explained.md` (que el equipo ya tiene).

Para MVP la física es **esfera vs planos analíticos**:
- suelo: plano `y = 0`
- pared: plano `x = X_WALL` (o `z = Z_WALL`)
- rebote: `v' = v - 2·(v·n)·n` con restitución `e` (`v'_n *= -e`), sin spin.
No hace falta el BVH para MVP.

---

## 6. Assets — ver [BUILD_GUIDE.md §3]. Para *PingPong RT*:

| Asset | ¿Necesario MVP? | Origen |
|---|---|---|
| skybox `null_plainsky512_*.png` ×6 | Sí (o fondo sólido con `skybox_enabled=false` + `bg_color`) | `trecnis/res/textures/skybox3/` |
| textura de mesa/madera | No (MVP: color plano) | crear/futuro |
| sonido de golpe / rebote (`.wav`) | No (fase 8+) | crear |
| OBJ de raqueta / jugador | No (MVP: cajas + procedural) | fase 9 |
| fuente 8×8 | Sí (ya vendored en `font8x8_basic.hpp`) | — |

**Ruta:** decidir `res/` (menos cambios) vs `assets/` (layout del enunciado) — el
agente de Integración lo fija en fase 0 y ajusta los 6 strings del skybox.

---

## 7. Dependencias — ver [BUILD_GUIDE.md §2 y §6]. Resumen de traslado:

Copiar a `pingpong-rt/`: `third_party/{SDL2,CL}` + `lib/{libSDL2*,libSDL2_mixer*,
libOpenCL}.a` + `bin/{SDL2,SDL2_mixer,libstdc++-6,libgcc_s_seh-1,libwinpthread-1}
.dll` + `.gitignore`. Embree: no copiar (arrancar `WITH_EMBREE=OFF`).

---

## 8. CMake — ver [BUILD_GUIDE.md §4]. Resumen: copiar el de trecnis, cambiar
`project(pingpong_rt)`, poner `-march=native` tras `option(NATIVE_ARCH)`.

---

## 9. Estructura propuesta de `pingpong-rt/`

```
pingpong-rt/
├── src/
│   ├── main.cpp                         ORIGEN trecnis · ADAPTAR (título ventana, quizá nada más) · MVP
│   ├── sr_config.hpp                    trecnis · copiar · MVP
│   ├── core/                            trecnis · copiar TODO tal cual · MVP
│   │   ├── sr_camera.{hpp,cpp}
│   │   ├── sr_framebuffer.hpp
│   │   ├── sr_geometry.{hpp,cpp}
│   │   ├── sr_texture.{hpp,cpp}
│   │   ├── sr_render_config.hpp
│   │   ├── sr_text.{hpp,cpp}
│   │   └── font8x8_basic.hpp
│   ├── math/                            trecnis · copiar TODO tal cual · MVP
│   │   ├── sr_math.hpp  sr_constants.hpp  sr_vec.hpp  sr_lingalg.hpp  sr_helpers.hpp
│   │   └── sr_aabb.hpp                  NUEVO · crear (helpers AABB para colisión) · 2ª fase
│   ├── render/                          trecnis · copiar TODO tal cual · MVP
│   │   ├── render_scene.hpp             (adaptar SOLO si el shading necesita datos nuevos)
│   │   ├── renderer.{hpp,cpp}
│   │   ├── raster/sr_raster.{hpp,cpp}
│   │   └── raytrace/                    bvh, accel, bvh_accel, cpu_tracer, sr_raytrace,
│   │                                    sr_ocl, embree_bvh, embree_accel  ← NO TOCAR bvh.*
│   ├── engine/
│   │   ├── anim/skinned_mesh.{hpp,cpp}  trecnis · copiar · 2ª fase (jugador)
│   │   ├── anim/camera_anim.{hpp,cpp}   trecnis · copiar · futuro (repeticiones)
│   │   ├── assets/obj_loader.{hpp,cpp}  trecnis · copiar + CABLEAR · 2ª fase
│   │   └── geom/shapes.{hpp,cpp}        NUEVO · add_sphere/add_box (de raytrec sr_scene.cpp) · MVP
│   ├── game/
│   │   ├── sr_game.{hpp}                trecnis · adaptar (misma API pública) · MVP
│   │   ├── game_state.hpp               ADAPTAR de sr_game_state.hpp (añade Player/Racket/Ball) · MVP
│   │   ├── game.cpp                     ADAPTAR de sr_game.cpp (loop callbacks + escena) · MVP
│   │   ├── hud.{hpp,cpp}                trecnis · adaptar (menú, +marcador) · 2ª fase
│   │   ├── court.{hpp,cpp}              NUEVO · suelo + pared (+ mesa/red futuro) → static_bvh · MVP
│   │   ├── player.{hpp,cpp}             NUEVO · CREAR · MVP (fase 3)
│   │   ├── racket.{hpp,cpp}             NUEVO · CREAR · MVP (fase 4)
│   │   ├── ball.{hpp,cpp}               NUEVO · CREAR · MVP (fase 5)
│   │   ├── physics.{hpp,cpp}            NUEVO · CREAR · MVP (fase 6)
│   │   ├── collision.{hpp,cpp}          NUEVO · CREAR · MVP (fase 7)
│   │   └── game_camera.{hpp,cpp}        NUEVO · CREAR · fase 10 (MVP: cámara fija inline)
│   ├── io/stb_image.{hpp,cpp}           trecnis · copiar tal cual · MVP
│   └── sound/sr_sound.{hpp,cpp}         trecnis · copiar tal cual · MVP (usar en fase 8)
├── assets/  (o res/)                    skybox ×6 (de trecnis) · MVP
├── third_party/  lib/  bin/             de trecnis (no en git) · MVP
├── tests/                               NUEVO · fase 6+ (física/colisión con asserts)
├── docs/                                estos 7 .md
├── CMakeLists.txt                       ADAPTAR de trecnis · MVP
└── README.md                            NUEVO
```

**Regla:** no crear archivos "por si acaso". `game_camera.*` puede empezar como
código inline en `game.cpp` y extraerse en fase 10. `tests/` empieza vacío.

---

## 10. Plan por fases

> Criterio transversal de "hecho": **compila con `-DWITH_EMBREE=OFF`, corre, y los
> 3 backends disponibles (Raster, CPU BVH, OpenCL) muestran lo mismo.**

### FASE 0 — Infraestructura (copiar el motor)
- **Archivos:** todo `core/ math/ render/ io/ sound/` de trecnis; `engine/anim/*`
  y `engine/assets/*`; `main.cpp`, `sr_config.hpp`; `CMakeLists.txt` adaptado;
  `third_party/ lib/ bin/`; skybox.
- **Objetivo:** el proyecto compila y arranca mostrando **solo el skybox** (o
  `bg_color`) — sin escena.
- **Dependencias:** [BUILD_GUIDE.md].
- **Hecho cuando:** `cmake --build` OK; ventana abre; `[G]`/`[TAB]` cicla backends
  sin crash; `[ESC]` cierra.
- **Riesgos:** SDL2/mixer/OpenCL mal colocados (FATAL_ERROR); `-march=native`;
  `res/` vs CWD.

### FASE 1 — Ventana + renderer vivos (escena vacía con 1 caja)
- **Archivos:** `game/game.cpp`, `game_state.hpp`, `sr_game.hpp` (adaptados,
  quitando personaje y point light orbitando).
- **Objetivo:** `game_rebuild_static` construye **una `add_box`** (cubo) en el
  origen; se ve con los 3 backends; cámara fija mirándolo.
- **Dependencias:** Fase 0.
- **Hecho cuando:** el cubo se ve idéntico en Raster / CPU BVH / OpenCL; HUD
  muestra FPS y nº de tris.
- **Riesgos:** `make_render_scene` mal portado; `Renderer::init` con
  `num_workers` sin resolver `--threads`.

### FASE 2 — Cancha: suelo + pared
- **Archivos:** `game/court.{hpp,cpp}` (NUEVO), `engine/geom/shapes.*` (NUEVO,
  `add_box` de raytrec).
- **Objetivo:** `Court` genera suelo (quad grande) + pared (cuboide vertical) →
  `static_bvh`. Cámara fija "detrás mirando a la pared".
- **Dependencias:** Fase 1.
- **Hecho cuando:** se ve suelo + pared con sombra del sol; sin parpadeos entre
  backends.
- **Riesgos:** winding CCW incorrecto (cara invisible); el suelo colapsa en una
  hoja del BVH (ya mitigado por `MAX_LEAF`).

### FASE 3 — Jugador (visible, movible, sin física)
- **Archivos:** `game/player.{hpp,cpp}` (NUEVO); reutiliza
  `build_procedural_character`.
- **Objetivo:** `Player` tiene `mesh` + `pos`; WASD lo mueven en el plano XZ con
  límites; entra al `dynamic_bvh` cada frame; animación idle (la onda procedural).
- **Dependencias:** Fase 2, `engine/anim/skinned_mesh`.
- **Hecho cuando:** el jugador camina por la cancha, se ve en los 3 backends, no
  atraviesa la pared (clamp de posición, aún sin colisión real).
- **Riesgos:** `build_dynamic` no foldeando al jugador; coste de rebuild si la
  mesh es densa (medir `dynamic_bvh` build ms en el HUD).

### FASE 4 — Raqueta
- **Archivos:** `game/racket.{hpp,cpp}` (NUEVO).
- **Objetivo:** `Racket` = caja fina, transform **relativo al jugador** (offset +
  orientación por el mouse); entra al `dynamic_bvh`; máquina de estados
  `idle/swing/cooldown` con animación de swing (lerp de `mesh.rotation`), sin
  efecto físico todavía.
- **Dependencias:** Fase 3; input de click (nuevo `case` en `game_handle_events`).
- **Hecho cuando:** la raqueta sigue al jugador; el click dispara la animación de
  swing y vuelve a idle.
- **Riesgos:** acumulación de transforms (jugador·raqueta) mal compuesta.

### FASE 5 — Pelota (visible, cae por gravedad, sin colisión)
- **Archivos:** `game/ball.{hpp,cpp}` (NUEVO); `engine/geom/shapes.*`
  (`add_sphere`).
- **Objetivo:** `Ball` = esfera (`add_sphere`, ~12×8), `pos`, `vel`; se regenera
  su geometría en `pos` cada frame y entra al `dynamic_bvh`. Sin física aún: se
  queda quieta o cae en línea recta si le pones `vel`.
- **Dependencias:** Fase 2.
- **Hecho cuando:** la esfera se ve redonda (smooth) en los 3 backends y se puede
  teletransportar cambiando `ball.pos`.
- **Riesgos:** regenerar tris cada frame es coste; alternativa: 1 mesh esfera fija
  + `mesh.setPosition` + `fold_mesh` (más barato). **Decidir y documentar.**

### FASE 6 — Física básica de la pelota
- **Archivos:** `game/physics.{hpp,cpp}` (NUEVO); `tests/test_physics.cpp` (NUEVO).
- **Objetivo:** integrador Euler semi-implícito (`v += a·dt; p += v·dt`),
  gravedad, drag lineal opcional. **Sin spin, sin Magnus** (fase 12). Parámetros
  del notebook (`G=9.8`, `DT=1/60`, `DRAG_COEF≈0.06`).
- **Dependencias:** Fase 5; referencia `RTT_Shooting_Method.ipynb §1-3`.
- **Hecho cuando:** una pelota lanzada describe una parábola estable; test unit
  compara la trayectoria con valores esperados.
- **Riesgos:** `dt` variable (usar sub-step fijo si el frame se alarga);
  divergencia si `dt` grande.

### FASE 7 — Colisiones (pelota ↔ pared / suelo)
- **Archivos:** `game/collision.{hpp,cpp}` (NUEVO), `math/sr_aabb.hpp` (NUEVO),
  `tests/test_collision.cpp`.
- **Objetivo:** esfera vs plano (suelo `y=0`, pared `x=X_WALL`). Detección
  (`dist(center, plane) <= r`) + respuesta (reflejar `vel` sobre la normal, con
  restitución). El peloteo se mantiene: golpe → pared → vuelve.
- **Dependencias:** Fase 6.
- **Hecho cuando:** la pelota rebota indefinidamente entre pared y una devolución;
  no atraviesa el suelo; tests de rebote (ángulo de entrada = ángulo de salida).
- **Riesgos:** tunneling a alta velocidad (usar test barrido rayo+radio contra el
  plano, o clamp de `dt`); doble rebote en una esquina.

### FASE 8 — Golpe de raqueta (cierra el MVP)
- **Archivos:** `game/racket.cpp` + `game/collision.cpp` (ampliar);
  `sound/sr_sound` (efecto de golpe).
- **Objetivo:** durante el estado `swing`, si la AABB de la raqueta interseca la
  esfera → impartir velocidad a la pelota hacia la pared (dirección = normal de la
  cara de la raqueta + algo de la velocidad del swing). `sound_play_sample` al
  golpear.
- **Dependencias:** Fases 4, 7.
- **Hecho cuando:** **el jugador puede mantener un peloteo contra la pared**
  (objetivo del enunciado). Sonido al golpear.
- **Riesgos:** ventana de tiempo del swing demasiado corta/larga; el impulso
  depende del framerate (normalizar por `dt`).

> **✅ Fin del MVP.** Fases 9-12 son evolución.

### FASE 9 — Animación (jugador riggeado)
- **Archivos:** `game/player.cpp` (ampliar), assets nuevos o `robloxian.obj` +
  `skin.*`.
- **Objetivo:** sustituir el placeholder procedural por `load_obj_mesh` +
  `SkinnedMesh::load` (patrón `raytrec/sr_scene.cpp:549`). Animaciones: idle,
  caminar, golpe.
- **Riesgos:** binding rig↔mesh por `src_vertex` (ver comentario en
  `skinned_mesh.hpp`); mismatch de nº de huesos (retorna `false`).

### FASE 10 — Cámara dinámica
- **Archivos:** `game/game_camera.{hpp,cpp}` (extraer de `game.cpp`).
- **Objetivo:** seguimiento suave del jugador (lerp de posición), o cámara fija
  cenital tipo RTT. Intro con `camera_anim` opcional.
- **Riesgos:** mareo por smoothing mal tuneado; recalcular `_aspectRatio` al
  redimensionar la ventana (hoy no se hace en trecnis).

### FASE 11 — Optimizar BVH (solo si hace falta)
- **Objetivo:** medir `dynamic_bvh` build ms (HUD). Si domina el frame: pool de
  tris reutilizado (evitar `clear`/realloc), o BVH dinámico por objeto + TLAS, o
  refit. **No reemplazar `bvh::BVH`** (regla). Documentar el porqué de cualquier
  cambio.
- **Riesgos:** romper la sincronía con el layout de `flatten` (GPU).

### FASE 12 — Física avanzada (spin, Magnus, mesa, red)
- **Archivos:** `game/physics.cpp`, `game/collision.cpp`, `game/court.cpp`.
- **Objetivo:** portar de `ball-physics-explained.md` / notebook: spin como
  vector `ω`, Magnus `a += k·(ω×v)`, rebote con acoplamiento tangencial
  (restitución 0.95, fricción 0.4), mesa (plano con altura), red (cápsula).
- **Riesgos:** el tuning es delicado (constantes reales en el .md); mantener el
  MVP jugable detrás de un flag.

---

## 11. Contrato Game ⇄ Renderer (conceptual, para respetar siempre)

```
GAME puede:                          GAME no debe:
  - crear/mover objetos               - llamar a bvh::BVH::intersect para PINTAR
  - fijar position/rotation/scale     - incluir <render/raytrace/sr_ocl.hpp> para render
  - asignar mesh y material            - implementar trazado de rayos
  - construir static_bvh / dynamic_bvh - conocer qué backend está activo (salvo HUD)
  - llenar un RenderScene
  - llamar renderer.render(scene, fb)

RENDERER puede:                      RENDERER no debe:
  - leer RenderScene                  - incluir nada de game/
  - trazar / rasterizar               - conocer reglas del juego (puntuación, saque…)
  - elegir backend                    - poseer o reconstruir los BVH
  - repartir el frame en hilos        - mutar el estado del juego
```

Formalización mínima (ya se cumple, mantener):
- `RenderScene` = struct POD en `render/render_scene.hpp`, solo tipos de
  `core/`+`render/raytrace/bvh.hpp`.
- `game/` incluye `render/renderer.hpp` y `render/render_scene.hpp`. Nada más de
  `render/raytrace/` salvo `bvh.hpp` (para construir los árboles).
- Un cambio de shading que necesite un dato nuevo → **campo nuevo en
  `RenderScene`**, nunca un `Game*`.

---

## 12. Riesgos y cómo evitarlos (sección 17 del encargo)

| Riesgo | Causa | Mitigación |
|---|---|---|
| **Romper el ray tracer CPU** | tocar `trace_ray` o `bvh.cpp` | `bvh.*` es intocable (regla). Cambios de shading solo añadiendo datos vía `RenderScene`; probar con `[TAB]` a "CPU SOFTWARE" tras cada cambio |
| **Romper OpenCL** | cambiar shading/materiales sin replicar en `KERNEL_SRC` | Checklist: todo cambio en `trace_ray` → mismo cambio en `sr_ocl.cpp` `KERNEL_SRC`, o aceptar y documentar la divergencia. Probar `[G]` |
| **Romper Embree** | linkado `embree4.lib` (MSVC) con MinGW | arrancar `WITH_EMBREE=OFF`; activarlo como fase aparte con su propio criterio de aceptación |
| **Incompatibilidad del BVH** | pasar geometría con NaN / triángulos degenerados | validar en `fold_mesh` (descartar tris con área ~0); `build` ya cap-ea profundidad |
| **Problemas de memoria (8 GB)** | `-j12` al compilar; escenas gigantes | `-j4`; MVP tiene < 5k tris; el ray tracer es compute-bound no memory-bound |
| **Rendimiento / frame rate** | ray tracer CPU a resolución alta | resolución baja (`--width 640`), `[G]` GPU, menú `[M]` (reflections off, bounces 0); rasterizar mientras se mueve |
| **Objetos dinámicos** | rebuild del `dynamic_bvh` cada frame domina el frame | medir en HUD; fase 11; MVP no llega ahí |
| **Sincronización física/render** | render lee `RenderScene` en N hilos mientras el juego lo muta | mantener el orden: **update completo → build BVH → render**. Nunca mutar escena en callbacks de render |
| **`dt` variable / tunneling** | frame largo → paso de integración grande → la pelota atraviesa la pared | sub-step fijo de física (acumulador); o test barrido esfera–plano; clamp `dt` a ~1/30 |
| **Assets / rutas** | `res/` relativo al CWD; mover carpeta rompe los 6 strings del skybox | ejecutar desde raíz (target `run`); centralizar en `asset_path()`; decidir `res/` vs `assets/` en fase 0 |
| **CMake** | `GLOB_RECURSE` no ve `.cpp` nuevos; `-march=native` no portable | `CONFIGURE_DEPENDS` (ya está); re-configurar tras añadir archivos; `option(NATIVE_ARCH)` |
| **Nombres colisionando** | ambos proyectos usan `bvh_raytracer`, `sr_config.hpp` | `project(pingpong_rt)`; el `inline global_config` es ODR-safe |
| **Regresión visual entre backends** | un cambio se ve distinto en raster vs tracer | el raster es "vista de depuración plana" por diseño (sin GI); comparar CPU BVH vs OpenCL, no vs raster |

---

## 13. Qué NO hacer (recordatorio del enunciado)

No implementar juego/pelota/física/gameplay en esta entrega · no cambiar el
renderer · no reemplazar el BVH · no eliminar backends · no refactors grandes ·
no borrar código · no inventar APIs · marcar lo no implementado ·
comparar implementaciones duplicadas (hecho: raytrec vs trecnis) ·
priorizar reutilización · C++17 · ray tracing hardware-agnostic · juego desacoplado
del renderer.
