# Plan: debugger con ImGui + GUI de juego dibujada por GPU

Estado: **implementado** (Fases 0–3 y 5; la Fase 4, overlays de debug por ImGui,
es opcional y quedó pendiente). Los gizmos y overlays de debug siguen dibujándose
en el framebuffer, después del punto de captura del dump. Ver AGENTS.md para las
invariantes actualizadas.

## Objetivo

1. **Debugger con Dear ImGui**, abierto/cerrado con la tecla **M**. Reemplaza el
   menú de opciones actual y el HUD de texto que se pinta en el framebuffer.
2. **Sistema de GUI del juego** (marcador, menú de título, pausa, banner de
   punto) que no pinta píxeles en CPU: genera geometría (quads texturizados) y la
   rasteriza la GPU vía `SDL_RenderGeometryRaw`.
3. El framebuffer queda con **solo la salida del backend**. Toda la UI se dibuja
   después de `SDL_RenderCopy`, a resolución de ventana.

"Sin CPU" significa: la CPU solo arma listas de vértices (cientos, microsegundos);
rasterizado, blending y escalado los hace la GPU.

## Decisiones ya tomadas (no reabrir)

- La tecla **M** abre/cierra el debugger ImGui. El menú viejo (`draw_menu`,
  `menu_label/value/apply`, `menu_cursor`, `MENU_ITEMS`) se **borra**.
- Backend de ImGui: `imgui_impl_sdl2` + `imgui_impl_sdlrenderer2` sobre el
  `SDL_Renderer` existente. **No** se crea contexto OpenGL.
- Fuente del juego: **primero bitmap 8x8** a partir de `font8x8_basic`
  ([src/core/font8x8_basic.hpp](../src/core/font8x8_basic.hpp)); TTF queda para
  después (Fase 6, fuera de este trabajo).
- ImGui vive **solo en `src/app/`**. El juego nunca incluye ImGui.
- `subsystems/ui/` es C++ puro: **nunca** incluye SDL.

## Contexto del código actual (verificado al escribir este plan)

- SDL bundled es **2.30** (`third_party/SDL2/SDL_version.h`); cumple el mínimo
  2.0.18 de `imgui_impl_sdlrenderer2` y de `SDL_RenderGeometryRaw`.
- En Windows `SDL_CreateRenderer(-1, ACCELERATED)` elige Direct3D, no OpenGL.
  Da igual para este plan: todo va por la API de `SDL_Renderer`.
- [src/main.cpp](../src/main.cpp):
  - `init_sdl()` usa `SDL_RenderSetLogicalSize` + `SDL_RenderSetIntegerScale`.
  - El bloque `SDL_RenderClear / SDL_RenderCopy / SDL_RenderPresent` está
    **repetido 3 veces**: loop principal y dos veces en `run_bench`.
- [src/app/app.cpp](../src/app/app.cpp) `app_render()`:
  clear → `build_frame` → `renderer.render` → **captura de dump** → gizmo
  (`render_gizmo`) → debug (`draw_bvh_debug`, `draw_normals_debug`) → HUD con
  `draw_text` → `draw_menu` → `SDL_UpdateTexture`.
- `app_handle_events()`: ESC cierra el menú o sale; **M** hace toggle de
  `show_menu` y del relative mouse mode; flechas/Enter navegan el menú; teclas
  TAB/G/B/V/H/N/L/F12.
- El juego (tenis de mesa, [src/game/game.cpp](../src/game/game.cpp)) mueve la
  paleta con `InputState::look_x/look_y` (deltas de mouse en modo relativo).
- `Profiler` en [src/core/sr_profiler.hpp](../src/core/sr_profiler.hpp): enum
  `ProfSection` con `PROF_HUD`, `PROF_GIZMO`, `PROF_DEBUG`, `PROF_UPLOAD`,
  `PROF_PRESENT`; nombres en `sr_profiler.cpp`.
- Bench de referencia (640x480, 12 workers, CPU SOFTWARE): trace 60.2 ms,
  upload 0.6, present 0.9, hud 0.6 (2.0 ms en la fila "HUD text").

## Invariante de verificación principal

La UI se dibuja **después** del punto de captura del dump, así que **el hash de
`--dump` de cada backend debe ser idéntico al baseline en todas las fases**. Si
cambia, algo dibujó en el framebuffer antes de la captura o se alteró el render:
es un bug, no una consecuencia esperada.

Antes de tocar nada:

```sh
cmake --build build
./build/bin/bvh_raytracer.exe --dump dumps_base > dumps_base/log.txt
./build/bin/bvh_raytracer.exe --bench 3 > dumps_base/bench.txt
```

Al final de cada fase: build limpio, `--dump dumps_phaseN` con hashes idénticos
a `dumps_base`, `--bench 3` sin regresiones, los greps de capas de AGENTS.md, y
abrir la app para mirar la UI (el dump no la captura, a propósito). Reportar
explícitamente qué se verificó y qué no.

---

## Fase 0 — Present propio y sin logical size

**Archivos:** `src/main.cpp`, `src/app/app.cpp`, `src/app/app.hpp`, `src/app/app_state.hpp`

1. Quitar `SDL_RenderSetLogicalSize` y `SDL_RenderSetIntegerScale` de `init_sdl()`.
2. Nueva función `void app_present(App* e, SDL_Renderer* r, SDL_Texture* fb_tex)`
   en `app/`:
   - `SDL_RenderClear`
   - calcula el rect destino: escala entera `s = max(1, min(win_w / fb.width, win_h / fb.height))`,
     centrado (letterbox). Guardarlo en `App` (`SDL_Rect present_rect`) porque la
     UI y el mapeo de mouse lo necesitan. Si la ventana es más chica que el fb,
     usar escala fraccional que entre (no recortar).
   - `SDL_RenderCopy(r, fb_tex, NULL, &present_rect)`
   - (punto de inserción de las fases siguientes: UI del juego, luego ImGui)
   - `SDL_RenderPresent`
   - todo dentro de `PROF_SCOPE(PROF_PRESENT)`.
3. Reemplazar los **tres** bloques Clear/Copy/Present de `main.cpp` por
   `app_present`. `run_bench` sigue midiendo "SDL present" alrededor de esa
   llamada.
4. `App` necesita acceso al `SDL_Renderer*`: pasarlo en `app_present` (no
   guardarlo como global nuevo). Actualizar el comentario de `app.hpp`, que dice
   que main.cpp llama "exactly these five functions".

**Aceptación:** ventana redimensionable sigue mostrando la imagen escalada y
centrada; hashes del dump idénticos; "SDL present" en el bench ±0.3 ms.

Commit: `App: own the present step, drop SDL logical size`

---

## Fase 1 — Vendorizar ImGui

**Archivos:** `third_party/imgui/`, `CMakeLists.txt`

1. Copiar Dear ImGui (último release estable, rama normal, no docking) a
   `third_party/imgui/`:
   `imgui.cpp imgui_draw.cpp imgui_tables.cpp imgui_widgets.cpp imgui_demo.cpp`,
   `imgui.h imgui_internal.h imconfig.h imstb_rectpack.h imstb_textedit.h imstb_truetype.h`,
   `backends/imgui_impl_sdl2.{h,cpp}`, `backends/imgui_impl_sdlrenderer2.{h,cpp}`,
   y `LICENSE.txt`. Anotar la versión en el mensaje de commit.
2. CMake (el glob de `src/` no lo cubre, hay que editarlo):
   ```cmake
   option(WITH_IMGUI "Dear ImGui debugger [M key]" ON)
   if(WITH_IMGUI)
       add_library(imgui STATIC <los .cpp de arriba>)
       target_include_directories(imgui PUBLIC
           "${CMAKE_SOURCE_DIR}/third_party/imgui"
           "${CMAKE_SOURCE_DIR}/third_party/imgui/backends")
       # los backends hacen #include <SDL.h>, no <SDL2/SDL.h>
       target_include_directories(imgui PRIVATE "${CMAKE_SOURCE_DIR}/third_party/SDL2")
       target_link_libraries(imgui PUBLIC SDL2::SDL2)
       target_compile_definitions(imgui PUBLIC ImDrawIdx=unsigned\ int)  # ver nota
       if(WIN32) target_link_libraries(imgui PUBLIC imm32) endif()
       target_compile_definitions(${PROJECT_NAME} PRIVATE WITH_IMGUI)
       target_link_libraries(${PROJECT_NAME} PRIVATE imgui)
   endif()
   ```
   - `ImDrawIdx` de 32 bits: el overlay de BVH (Fase 4) puede pasar de 64k
     vértices. Si la sintaxis del define da problemas, ponerlo en `imconfig.h`
     (`#define ImDrawIdx unsigned int`). Debe ser igual en todas las TU.
   - `imm32` es para el IME de Windows que usa `imgui.cpp`.
   - No compilar ImGui con `-ffast-math`/`-march=native` de la app: son opciones
     del target `bvh_raytracer`, no globales; verificar que siga así.
3. Con `-DWITH_IMGUI=OFF` el proyecto debe compilar y correr igual que hoy (sin
   debugger). Todo el código ImGui de `app/` va bajo `#ifdef WITH_IMGUI`.

**Aceptación:** build con ON y con OFF (segundo build dir).

Commit: `Vendor Dear ImGui <versión> with the SDL2 + SDL_Renderer backends`

---

## Fase 2 — Debugger ImGui en `app/`

**Archivos nuevos:** `src/app/app_debug.hpp`, `src/app/app_debug.cpp` (el
**único** `.cpp` que incluye `imgui.h`).
**Archivos tocados:** `app.cpp`, `app_hud.cpp/.hpp`, `app_state.hpp`, `main.cpp`,
`core/sr_profiler.{hpp,cpp}`.

### 2a. Ciclo de vida
- `app_debug_init(App*, SDL_Window*, SDL_Renderer*)` tras `init_sdl`/`app_init`:
  `ImGui::CreateContext`, `ImGui_ImplSDL2_InitForSDLRenderer`,
  `ImGui_ImplSDLRenderer2_Init`. Desactivar `imgui.ini` (`io.IniFilename = nullptr`)
  para no ensuciar el repo.
- `app_debug_event(App*, const SDL_Event&)`: llama
  `ImGui_ImplSDL2_ProcessEvent`. Se llama al principio de `app_handle_events`.
- `app_debug_frame(App*)`: `NewFrame` de ambos backends + `ImGui::NewFrame`,
  construye las ventanas, `ImGui::Render`. Llamar desde `app_present` justo
  antes de `SDL_RenderPresent` y luego
  `ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer)`.
- `app_debug_shutdown()` en el apagado.
- `window` y `renderer` son globales en `main.cpp`; pasarlos como argumento, no
  exponerlos como `extern`.
- Medir todo con una sección nueva `PROF_IMGUI` (añadir al enum y a los nombres).

### 2b. Input y tecla M
- `App::show_menu` pasa a llamarse `show_debugger`. **M** lo alterna y cambia
  `SDL_SetRelativeMouseMode` (abierto → cursor libre).
- ESC: si el debugger está abierto lo cierra; si no, sale (igual que hoy).
- Con el debugger abierto, `poll_input` debe dejar `move`, `look_x/look_y` y
  `wheel` en cero cuando `io.WantCaptureMouse` / `io.WantCaptureKeyboard` lo
  indiquen, y en general mientras esté abierto (el cursor libre no debe mover la
  paleta). La simulación sigue corriendo.
- Las teclas sueltas (TAB/G/B/V/H/N/L/F12) se ignoran si
  `io.WantCaptureKeyboard` (por ejemplo, al escribir en un campo).
- Borrar la navegación con flechas del menú viejo.

### 2c. Ventanas del debugger (con M abierto)
1. **Render** — reemplaza exactamente el menú viejo y algo más:
   backend (combo con `renderer.count/name/available/select`), BVH vs fuerza
   bruta, mostrar BVH + profundidad (`bvh_debug_depth`), normales, reflexiones,
   sol, GI, `max_bounces` (1–3), estrategia estática (`set_static_strategy` +
   `commit_static`, como hace `menu_apply` caso 5) y dinámica. Cualquier toggle
   de `RenderOpts` que hoy solo tenga hotkey también va aquí.
2. **Profiler** — tabla de `g_prof.ms(i)` por sección + total, y `PlotLines`
   del frametime. Para el gráfico, guardar un ring buffer de ~240 muestras en
   `App` (no tocar `Profiler` salvo para añadir secciones).
3. **Escena** — posición/orientación de cámara (solo lectura), conteos y build
   time de ambos BVH (lo que hoy imprime el HUD largo), botón "Dump frame"
   (pone `dump_next = true`, igual que F12).
4. **Juego** — dibuja las variables de `DebugVars` (2e).

### 2d. HUD como overlay ImGui
- El HUD de texto de `app_render` (versión corta y larga, tecla **H**) pasa a una
  ventana ImGui sin decoración, semitransparente, anclada arriba a la izquierda,
  visible con `show_hud` aunque el debugger esté cerrado. Mismo contenido que
  hoy (FPS, backend, `game_status`, profiler, BVH, ayuda de teclas actualizada:
  `[M] debugger`, quitar `[SPACE x2] walk/fly`, que ya no existe).
- Borrar de `app_render` todo el bloque de HUD con `draw_text` y `draw_menu`, y
  de `app_hud.cpp` `draw_menu`, `fill_rect_alpha`, `menu_*`, `MENU_ITEMS`.
  `PROF_HUD` pasa a medir el armado del overlay o se elimina si queda en cero;
  actualizar `run_bench` ("HUD text") en consecuencia.
- Con `WITH_IMGUI=OFF` no hay HUD ni debugger. Es aceptable.
- `sr_text` deja de usarse en `app/`; **no borrarlo** (la GUI del juego reusa
  `font8x8_basic`, y puede servir de fallback).

### 2e. `DebugVars`: el juego expone parámetros sin ver ImGui
- Nuevo `src/subsystems/debug/debug_vars.hpp` (C++ puro, sin SDL ni ImGui):
  ```cpp
  struct DebugVar { const char* group; const char* name;
                    enum Kind { Float, Int, Bool } kind; void* ptr;
                    float min, max; };
  struct DebugVars { std::vector<DebugVar> vars;
                     void add(const char* g, const char* n, float* p, float lo, float hi);
                     void add(const char* g, const char* n, int* p, int lo, int hi);
                     void add(const char* g, const char* n, bool* p); };
  ```
- Nueva función de juego `void game_debug_vars(Game*, DebugVars&)`, declarada en
  `game.hpp` y llamada una vez desde `app_init` después de `game_init`.
  Registrar al menos: `paddle_ai_speed`, `kPaddleMouseSens` (pasarla a miembro de
  `Game`), `serve_hold`/`kServePause`, `table_rough`, `floor_rough`,
  `floor_metallic`. Los `constexpr` de física en `game_ball.hpp` **no** se tocan
  en este trabajo.
- El panel **Juego** recorre `vars` agrupando por `group` con
  `SliderFloat/SliderInt/Checkbox`.
- Los punteros apuntan a miembros de `Game`, que vive hasta `game_destroy`;
  vaciar `DebugVars` en `app_shutdown` antes de destruir el juego.

**Aceptación Fase 2:** M abre/cierra el debugger; todo lo que hacía el menú viejo
se puede hacer desde el panel Render; la paleta no se mueve con el debugger
abierto; H alterna HUD corto/largo; hashes del dump idénticos al baseline.

Commits sugeridos:
1. `App: ImGui debugger on [M] replaces the options menu`
2. `App: HUD moves from framebuffer text to an ImGui overlay`
3. `Subsystems: DebugVars registry; game exposes tunables to the debugger`

---

## Fase 3 — GUI del juego: `subsystems/ui/`

GUI inmediata propia, independiente de ImGui (funciona con `WITH_IMGUI=OFF`).

### 3a. Tipos (`src/subsystems/ui/ui_types.hpp`)
```cpp
using UiTexId = uint32_t;            // 0 = atlas de fuente (incluye un texel blanco)
struct UiVertex { float x, y, u, v; uint32_t rgba; };   // rgba: 0xAABBGGRR
struct UiCmd    { UiTexId tex; uint32_t idx_offset, idx_count;
                  float clip_x0, clip_y0, clip_x1, clip_y1; };
struct UiDrawList { std::vector<UiVertex> vtx; std::vector<uint32_t> idx;
                    std::vector<UiCmd> cmds; void clear(); };
```
`UiVertex` debe poder pasarse a `SDL_RenderGeometryRaw` con strides (xy, color,
uv) sin copiar; documentarlo con `static_assert` de `offsetof` en `app_ui.cpp`,
no en el header de `subsystems/`.

### 3b. Imágenes (`ui_image.{hpp,cpp}`)
- Registro `UiImages`: `UiTexId add(int w, int h, std::vector<uint32_t> rgba)`,
  `UiTexId load_png(const char* path)` (con el `stb_image` de `src/io/`), y por
  imagen `{w, h, pixels, version}`. `version` sube si se modifican los píxeles.
- El juego carga sus imágenes en `game_init` (es contenido); `app/` solo las sube.

### 3c. Fuente bitmap (`ui_font.{hpp,cpp}`)
- Construir en CPU, una vez, un atlas de 16x8 celdas de 8x8 (ASCII 0–127) desde
  `font8x8_basic`, más un bloque de texeles blancos para rects sólidos.
  Registrarlo en `UiImages` como `UiTexId 0`.
- Bits: `font8x8_basic[c][row] & (1 << col)`, igual que `is_char_pixel` en
  `sr_text.cpp`.
- Escala de texto **entera** (1x, 2x, 3x…) y muestreo **nearest** para que se
  vea nítido. Sombra/contorno opcional, igual que `draw_text` (offset 1–2 px en
  negro semitransparente).

### 3d. Contexto inmediato (`ui_context.{hpp,cpp}`)
- Resolución virtual: `ui_scale = max(1, floor(min(win_w/640, win_h/360)))`,
  `virtual = window / ui_scale`. Todo el layout es en píxeles virtuales; al
  emitir vértices se multiplica por `ui_scale`. Así la fuente 8x8 queda en
  escala entera.
- API mínima:
  ```cpp
  void begin_frame(const UiInput&, int win_w, int win_h);
  void rect(Rect, uint32_t rgba);
  void image(Rect, UiTexId, uint32_t tint = 0xFFFFFFFF);
  void text(Vec2 pos, const char* s, int scale, uint32_t rgba, Align = Left);
  Vec2 text_size(const char* s, int scale);
  bool button(Id, Rect, const char* label);   // true en el frame del click/aceptar
  Rect anchor(Anchor, Vec2 size, Vec2 margin); // TopLeft, Top, TopRight, Center, Bottom...
  void push_clip(Rect); void pop_clip();
  void request_pointer(bool visible);          // el juego pide cursor libre
  const UiDrawList& end_frame();
  ```
- Navegación sin mouse: `button` se vuelve "focused" con `nav_up/nav_down` en
  orden de declaración; `accept` lo activa. Hover del mouse también da foco.
- Sin estado persistente complejo: solo `hot`/`active`/`focused` por id, como
  ImGui.

### 3e. Entrada (`src/subsystems/input.hpp`)
Añadir a `InputState`:
```cpp
struct UiInput { float pointer_x = -1, pointer_y = -1;   // píxeles de ventana
                 bool  pointer_down = false, pointer_pressed = false;
                 bool  nav_up = false, nav_down = false,
                       accept = false, back = false; } ui;
```
Mapeo en `poll_input` / `app_handle_events`: mouse absoluto (solo cuando no está
en modo relativo), flechas/WASD de navegación, Enter/Espacio → accept,
Backspace → back. **ESC sigue siendo de `app/`** (cerrar debugger / salir) en
este trabajo. Si el debugger está abierto, `ui` va vacío.

### 3f. Submit en `app/` (`src/app/app_ui.{hpp,cpp}`)
- Mapa `UiTexId → SDL_Texture*` con la versión subida; crear con
  `SDL_TEXTUREACCESS_STATIC`, `SDL_PIXELFORMAT_ABGR8888` (coincide con `rgba`
  0xAABBGGRR), `SDL_SetTextureBlendMode(BLEND)` y
  `SDL_SetTextureScaleMode(SDL_ScaleModeNearest)`. Re-subir si cambia la versión.
- Por cada `UiCmd`: `SDL_RenderSetClipRect`, `SDL_RenderGeometryRaw` con
  punteros y strides sobre `vtx` (color como `SDL_Color`, que en memoria es
  RGBA: verificar el orden de bytes de `rgba` y ajustar la constante en un solo
  lugar).
- Al terminar, `SDL_RenderSetClipRect(r, NULL)`.
- Si el juego pidió `request_pointer(true)`, `app/` desactiva el modo relativo;
  si no (y el debugger está cerrado), lo activa.
- Orden dentro de `app_present`: `RenderCopy` del fb → **UI del juego** →
  **ImGui** → `Present`. El debugger siempre queda encima.
- Sección de profiler nueva `PROF_UI` (armado + submit).

### 3g. Hook del juego
- Nueva función en `game.hpp`: `void game_ui(Game*, UiContext&)`. Se llama en
  `app_render` (o `app_present`) cada frame, después de `game_update`.
- Pantallas, como máquina de estados dentro de `Game`
  (`enum class Screen { Title, Playing, Paused, PointBanner }`):
  - **Title:** título, botones "Jugar" y "Salir". "Salir" pone un flag que
    `app/` lee con `bool game_wants_quit(const Game*)`; el juego no llama SDL.
  - **Playing:** marcador `points_player – points_ai` arriba al centro,
    indicador de quién saca, `game_status`.
  - **PointBanner:** "Punto Jugador"/"Punto IA" durante `serve_hold`.
  - **Paused:** con `ui.back`; botones "Continuar" y "Salir al título".
- En Title/Paused la simulación del rally no avanza (decisión de `game_update`)
  y el juego llama `request_pointer(true)`.
- Fuente de verdad del marcador: los campos ya existentes en `Game`. No duplicar.

**Aceptación Fase 3:** el juego arranca en Title; se juega con el mouse; el
marcador se ve nítido a cualquier tamaño de ventana; el texto no se escala con el
framebuffer de 640x480; `-DWITH_IMGUI=OFF` sigue mostrando la GUI del juego;
hashes del dump idénticos (el dump no corre `game_ui`, o si lo corre no afecta al
fb).

Commits sugeridos:
1. `Subsystems: immediate-mode UI with an 8x8 bitmap font atlas`
2. `App: submit the UI draw list through SDL_RenderGeometryRaw`
3. `Game: title, scoreboard, point banner and pause screens`

---

## Fase 4 — Overlays de debug fuera del framebuffer (opcional, al final)

- `render_gizmo`, `draw_bvh_debug` y `draw_normals_debug` dejan de pintar en el
  fb y pasan a `ImGui::GetBackgroundDrawList()->AddLine`, proyectando con la
  cámara de `e->scene.cam()` a coordenadas del fb y luego a ventana con
  `present_rect`. Recortar líneas detrás de la cámara (near plane) como hace
  `draw_gizmo_line`.
- Si se hace, el framebuffer queda 100% salida del backend, y `PROF_GIZMO` /
  `PROF_DEBUG` miden solo el armado de la draw list.
- Si el volumen de líneas del BVH hace lento a ImGui, dejar BVH en el fb y mover
  solo gizmo y normales. Documentar la decisión.

Commit: `App: gizmo and debug lines drawn by ImGui instead of into the framebuffer`

---

## Fase 5 — Documentación

Actualizar [AGENTS.md](../AGENTS.md) y [README.md](../README.md):
- Diagrama estático: `subsystems/ui` y `subsystems/debug` (sin SDL);
  `app_debug.cpp` y `app_ui.cpp` en `app/`.
- Diagrama por frame: `game_ui` → `UiDrawList` → `app_present` → UI → ImGui.
- "Swapping the game module means changing the four `game_*` calls" → actualizar
  la cuenta (`game_ui`, `game_debug_vars`, `game_wants_quit`).
- Where things live / Which files to touch: "Nueva pantalla de juego",
  "Nuevo parámetro ajustable", "Nuevo panel de debug".
- Invariante nuevo: ImGui solo en `app/`; `subsystems/ui` sin SDL; la UI se
  dibuja después de `RenderCopy` y nunca en el framebuffer.
- Greps nuevos en "Verifying the layer boundaries":
  ```sh
  # ImGui only in app/
  grep -rln 'imgui' src | grep -v '^src/app/'
  # subsystems/ui and subsystems/debug never see SDL
  grep -rn 'SDL_\|#include.*SDL' src/subsystems/ui src/subsystems/debug
  ```
- Controles en README: M = debugger, H = HUD.
- Mover este archivo a "implementado" o borrarlo.

Commit: `Docs: ImGui debugger and GPU-drawn game UI`

---

## Fase 6 — TTF (después; NO es parte de este trabajo)

Vendorizar `stb_truetype.h` en `src/io/` (no reutilizar `imstb_truetype.h` de
ImGui, que es opcional), hornear un atlas por tamaño en `ui_font`, filtro
linear para esas texturas. La API de `UiContext` no debería cambiar.

## Fuera de alcance

- Pipelining asíncrono de OpenCL, interop CL/GL, contexto OpenGL propio.
- Gamepad (la `UiInput` ya está pensada para mapearlo después).
- Cambios de shading o del kernel (no aplica la regla CPU→OpenCL aquí).

## Riesgos conocidos

- **Coordenadas de mouse con letterbox**: ImGui recibe coordenadas de ventana y
  está bien; la proyección de overlays (Fase 4) debe pasar por `present_rect`.
- **Orden de bytes del color** en `SDL_RenderGeometryRaw` (`SDL_Color` es
  r,g,b,a en memoria): un error se ve como rojo↔azul invertidos; centralizar la
  conversión.
- **Conflicto de input**: prioridad debugger > UI del juego > juego. Probar
  abrir M en medio de un rally.
- **Driver AMD con OpenCL** (ver Gotchas de AGENTS.md): para verificaciones
  desatendidas usar un build con `-DWITH_OPENCL=OFF`.

## Reglas de commits

Un commit por paso, cada uno compilando. Sin trailers de co-autor ni de
atribución de herramienta (AGENTS.md, sección Commits).
