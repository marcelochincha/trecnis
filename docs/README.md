# pingpong-rt-docs — análisis del proyecto base para *PingPong RT*

Estos 7 documentos son el **hand-off** para construir `pingpong-rt/` (juego de
tenis de mesa en tiempo real con el ray tracer propio) reutilizando el código de
`trecnis/` (base) y `raytrec/` (piezas sueltas).

**No contienen código de juego.** Solo análisis y plan. Todo está verificado
contra el fuente real (`trecnis` commit `52d2a17`, `raytrec` commit `e586878`).

## Orden de lectura

| # | Documento | Para qué |
|---|---|---|
| 1 | [ARCHITECTURE.md](ARCHITECTURE.md) | Qué es cada módulo, el contrato game⇄render, por qué `trecnis` es la base, cómo encaja *PingPong RT* sin tocar el renderer |
| 2 | [CODE_INVENTORY.md](CODE_INVENTORY.md) | Tabla archivo por archivo: reutilizar / adaptar / no copiar / crear, dependencias, rutas fijas, código muerto |
| 3 | [RENDERER_API.md](RENDERER_API.md) | `RenderScene`, cómo se agregan meshes, cámara, backends, CPU tracer, OpenCL, Embree; qué interfaz conservar |
| 4 | [BVH_GUIDE.md](BVH_GUIDE.md) | Estructuras, build (SAH/Median/Morton), traversal, patrón de 2 árboles, cómo usarlo para pelota/raqueta/jugador/pared, cómo usarlo para colisiones |
| 5 | [BUILD_GUIDE.md](BUILD_GUIDE.md) | CMake actual desglosado, dependencias (SDL2/mixer/OpenCL/Embree), assets y rutas que se rompen, CMake propuesto, qué copiar |
| 6 | [MIGRATION_PLAN.md](MIGRATION_PLAN.md) | Personaje/cámara/input/game-loop actuales; qué física existe (poca); estructura propuesta; **plan de 13 fases** (0 → MVP en fase 8 → avanzado); riesgos |
| 7 | [AGENT_HANDOFF.md](AGENT_HANDOFF.md) | 9 agentes: archivos que puede/no puede tocar, interfaces a respetar, dependencias, criterios de aceptación |

## Conclusiones de una línea

- **Base:** `trecnis` (seam limpio `game → RenderScene → Renderer → backends`).
- **De `raytrec` se copian:** `add_sphere`, `add_box`, `add_humanoid`, y el patrón
  de carga OBJ+`.mtl` (en `trecnis` ese cargador existe pero **nadie lo llama**).
- **No tocar:** `render/raytrace/bvh.{hpp,cpp}`, ni el resto de `render/` salvo
  añadir campos a `RenderScene` si el shading lo necesita.
- **Ya existe y se reutiliza:** cámara, matemáticas, framebuffer, BVH, ray tracer,
  3 backends, skinning, rasterizador, sonido, HUD, cargador OBJ, `stb_image`.
- **Hay que crear desde cero:** `Ball`, `Racket`, `Player` (wrappers de juego),
  `Physics`, `CollisionSystem` (no hay física de sólidos ni colisiones de
  volúmenes — solo rayos y vectores), `Court`, cámara de juego.
- **El diseño de la física** ya está hecho: `RTT_Shooting_Method.ipynb` +
  `ball-physics-explained.md` (en `primera-parte/`).
- **MVP = fases 0-8** del plan de migración: peloteo del jugador contra una pared.
