# PingPong RT

Juego de tenis de mesa en tiempo real (C++17) sobre un motor de *ray tracing*
propio, hardware-agnóstico, heredado de los proyectos `raytrec` / `trecnis`.

> **Estado actual: Fase 0 — migración de infraestructura completada.**
> Existe una aplicación mínima que arranca el motor (ventana + renderer +
> skybox). **El gameplay todavía NO está implementado**: no hay jugador,
> pelota, raqueta, física ni colisiones. Ver *Estado actual* más abajo.

---

## Descripción

El objetivo del MVP es que un jugador 3D golpee una pelota contra una pared y
mantenga un peloteo, todo renderizado en tiempo real con el ray tracer propio.

Regla de arquitectura central:

```
Game  →  RenderScene  →  Renderer  →  CPU BVH / OpenCL / (Embree) / Raster
```

- La lógica de juego nunca implementa ray tracing.
- El renderer nunca contiene reglas de juego.
- El único puente es `RenderScene` (POD de punteros prestados).

Documentación detallada en `docs/` (empezar por `docs/README.md`).

---

## Requisitos

| Dependencia | Versión / notas |
|---|---|
| Compilador C++17 | MinGW-w64 GCC 11.1.0 (probado) |
| CMake | ≥ 3.16 (el bundle de CLion 3.28.1 sirve) |
| Generador | MinGW Makefiles (`mingw32-make`) |
| SDL2 | 2.32.10 — *bundled* en `third_party/` + `lib/` + `bin/` |
| SDL2_mixer | 2.8.2 — *bundled* |
| OpenCL | headers Khronos + `lib/libOpenCL.a` — *bundled*; runtime = driver GPU |
| Embree 4 | **opcional, OFF por defecto** (no incluido) |

`third_party/`, `lib/` y `bin/` **no se versionan** (`.gitignore`). Se copian a
mano desde `trecnis/` (ver `docs/BUILD_GUIDE.md §6`). Ya están colocados en este
árbol de trabajo.

CMake y Ninja **no están en el PATH global**: viven dentro de la instalación de
CLion. Añade a la terminal:

```
C:\Program Files\JetBrains\CLion 2024.1\bin\cmake\win\x64\bin
```

o abre la carpeta directamente en CLion.

---

## Estructura

```
pingpong-rt/
├── src/
│   ├── main.cpp              bootstrap mínimo (Fase 0) — sin gameplay
│   ├── sr_config.hpp         parseo de --width/--height/--fps/--threads
│   ├── core/                 cámara, framebuffer, geometría, texturas, texto  [motor, congelado]
│   ├── math/                 vec/mat header-only                              [motor, congelado]
│   ├── render/               Renderer + RenderScene + backends                [motor, congelado]
│   │   └── raytrace/bvh.*    BVH — INTOCABLE (caja negra)
│   ├── engine/anim/          skinning + animación de cámara                    [motor]
│   ├── engine/assets/        cargador OBJ + .mtl                              [motor]
│   ├── io/                   stb_image
│   └── sound/                wrapper SDL2_mixer
├── res/textures/skybox3/     6 caras del cubemap del cielo
├── docs/                     análisis + plan de migración + física
├── third_party/ lib/ bin/    dependencias bundled (no en git)
├── .claude/agents/           agentes de desarrollo por rol
└── CMakeLists.txt
```

Módulos de juego **pendientes** (se crearán en fases siguientes, en `snake_case`):
`game/court.*`, `game/player.*`, `game/racket.*`, `game/ball.*`,
`game/physics.*`, `game/collision.*`, `game/game_camera.*`, `game/game.cpp`,
`game/game_state.hpp`, `game/hud.*`. La pared pertenecerá a `Court`, no a un
módulo `Wall` aparte.

---

## Configuración

```powershell
$env:Path = "C:\Program Files\JetBrains\CLion 2024.1\bin\cmake\win\x64\bin;" + $env:Path

cmake -S . -B build -G "MinGW Makefiles" `
  -DCMAKE_BUILD_TYPE=Release `
  -DWITH_EMBREE=OFF `
  -DWITH_OPENCL=ON
```

Opciones de CMake:

| Opción | Default | Efecto |
|---|---|---|
| `WITH_OPENCL` | OFF (usar `ON`) | backend GPU OpenCL (tecla `[G]`) |
| `WITH_EMBREE` | OFF | backend Embree (no incluido todavía) |
| `NATIVE_ARCH` | ON | `-march=native` (binario no portable); `OFF` para portabilidad |
| `WIN32_GUI` | OFF | oculta la consola |

---

## Compilación

```powershell
cmake --build build -j4        # -j4: la máquina de desarrollo tiene 8 GB
```

El ejecutable queda en `build/bin/pingpong_rt.exe` con sus DLLs al lado.

---

## Ejecución

```powershell
build\bin\pingpong_rt.exe --width 960 --height 540 --threads -1
```

o, para que resuelva `res/` desde la raíz del repo:

```powershell
cmake --build build --target run
```

Flags: `--width`, `--height`, `--fps`, `--threads` (`-1` = todos los núcleos),
`--audio-rate`, `--debug`, `--help`.

Controles del bootstrap actual:

| Tecla | Acción |
|---|---|
| `ESC` | salir |
| `TAB` | backend anterior |
| `G`   | backend siguiente |

---

## Backends

`Renderer` crea, en orden: `RASTER` (0), `CPU SOFTWARE` (1, SAH BVH + pool de
hilos), `OCL GPU` (último). Arranca en `CPU SOFTWARE`. Embree se insertaría en
la posición 2 si `WITH_EMBREE=ON`. Se ciclan con `TAB` / `G`; los no disponibles
se saltan.

En la escena vacía de Fase 0 los tres muestran solo el skybox.

---

## Estado actual

**Hecho (Fase 0):**

- Motor migrado de `trecnis` sin modificar (`core/ math/ render/ io/ sound/
  engine/`).
- `CMakeLists.txt` adaptado (`project(pingpong_rt)`, `NATIVE_ARCH`,
  `WITH_EMBREE=OFF`, `WITH_OPENCL=ON`).
- `src/main.cpp` mínimo: SDL + framebuffer + cámara + skybox + `Renderer` +
  `RenderScene` vacío. Compila y arranca; los 3 backends se inicializan.
- Repositorio git inicializado.

**Pendiente (siguientes fases, ver `docs/MIGRATION_PLAN.md`):**

- Fase 1: capa `game/` mínima (1 caja en el BVH estático, cámara fija).
- Fase 2: `Court` (suelo + pared).
- Fases 3-8: Player, Racket, Ball, Physics, Collision, golpe de raqueta → MVP.

**NO implementado a propósito:** Player, Ball, Racket, Physics, Collision,
Score, IA, cualquier regla de gameplay.
