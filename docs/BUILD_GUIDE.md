# BUILD_GUIDE.md — CMake, dependencias, assets y traslado a nueva carpeta

> Fuente: `trecnis/CMakeLists.txt` (186 líneas), estado real del entorno de
> desarrollo (2026-09), y lo que se colocó a mano para que trecnis compile.

---

## 1. El CMake actual de trecnis — qué hace cada bloque

| Líneas | Bloque | Qué hace |
|---|---|---|
| 1-11 | proyecto | `project(bvh_raytracer LANGUAGES CXX)`, C++17, `CMAKE_CXX_EXTENSIONS OFF`, `Release` si no hay build type |
| 13-51 | **SDL2** | `find_package(SDL2 QUIET)`; si falla, busca `third_party/SDL2/SDL.h` + `lib/libSDL2*.a` y crea targets `SDL2::SDL2` / `SDL2::SDL2main` IMPORTED. **`FATAL_ERROR` si no lo encuentra.** |
| 53-65 | **SDL2_mixer** | Si existe `lib/libSDL2_mixer.dll.a` → target `SDL2_mixer::SDL2_mixer`. Dice "building without audio" si falta, **pero el código lo necesita igual** (`sr_sound.hpp` incluye `<SDL2/SDL_mixer.h>` sin guardas). |
| 67-76 | **fuentes** | `file(GLOB_RECURSE APP_SOURCES CONFIGURE_DEPENDS src/*.cpp)` → 1 `add_executable(${PROJECT_NAME})`. `option(WIN32_GUI)` para ocultar consola. |
| 78 | includes | `target_include_directories(... src)` → todos los `#include <core/...>` etc. son relativos a `src/`. |
| 80-87 | link SDL | `SDL2::SDL2 SDL2_mixer::SDL2_mixer` (+ `SDL2main` si existe) + `mingw32 psapi` en Windows. |
| 89-116 | **OpenCL** | `option(WITH_OPENCL OFF)`. En Windows busca `lib/libOpenCL.a` (o del sistema); define `ENABLE_OPENCL`, añade `third_party/` al include, linka. En macOS: `-framework OpenCL`. |
| 118-135 | **Embree** | `option(WITH_EMBREE ON)`. Si existe `third_party/embree4/rtcore.h` + `lib/embree4.lib` → define `WITH_EMBREE`, linka. Si no, "building without it". |
| 138-147 | flags | `target_compile_options(-march=native -ffast-math)`. `/W3 /utf-8` + `_CRT_SECURE_NO_WARNINGS` si MSVC. |
| 149-151 | salida | binario a `${CMAKE_BINARY_DIR}/bin`. |
| 153-177 | **DLLs runtime (Windows)** | copia `bin/{SDL2,libwinpthread-1,SDL2_mixer,libstdc++-6,libgcc_s_seh-1}.dll` y `bin/{embree4,tbb12,tbbmalloc}.dll` junto al `.exe` en post-build, si existen. |
| 179-185 | target `run` | `cmake --build . --target run` ejecuta con `WORKING_DIRECTORY = ${CMAKE_SOURCE_DIR}` (para que encuentre `res/`). |

### Macros / defines que el código consulta
| Macro | Origen | Efecto |
|---|---|---|
| `WITH_EMBREE` | CMake `-DWITH_EMBREE=ON` (default ON) | compila `embree_bvh.hpp` / `embree_accel.hpp` y el backend Embree |
| `ENABLE_OPENCL` | CMake `-DWITH_OPENCL=ON` (default OFF) | (define presente; `sr_ocl.cpp` se compila siempre pero solo enlaza CL si está) |
| `W_WIDTH` / `W_HEIGHT` | `#define` en `sr_config.hpp` (480×360) | tamaño de ventana por defecto (sobre-escribible por `-DW_WIDTH=`) |
| `_CRT_SECURE_NO_WARNINGS` | MSVC only | silencia warnings de CRT |

---

## 2. Dependencias — tabla completa

| Dependencia | ¿Obligatoria? | Dónde está ahora | Cómo se configura | Archivos que necesita |
|---|---|---|---|---|
| **Compilador C++17** | Sí | MinGW-W64 GCC 11.1.0 (`C:\mingw64\mingw64\mingw64\bin`) | `-std=c++17` en CMake | — |
| **CMake ≥ 3.16** | Sí | **solo el de CLion** 2024.1 (`...\CLion 2024.1\bin\cmake\win\x64\bin`). No hay global. | añadir al PATH o `winget install Kitware.CMake` | — |
| **SDL2** | **Sí** (FATAL_ERROR) | `trecnis/third_party/SDL2/` (headers) + `trecnis/lib/libSDL2*.a` + `trecnis/bin/SDL2.dll`. Versión colocada: **2.32.10** (build MinGW) | bundled; CMake lo detecta en `third_party/`+`lib/` | headers `SDL*.h`, `libSDL2.dll.a`, `libSDL2.a`, `libSDL2main.a`, `SDL2.dll` |
| **SDL2_mixer** | **Sí en la práctica** (código lo `#include` sin guardas) | `trecnis/third_party/SDL2/SDL_mixer.h` + `trecnis/lib/libSDL2_mixer.dll.a` + `trecnis/bin/SDL2_mixer.dll`. Versión: **2.8.2** (MinGW) | bundled | `SDL_mixer.h`, `libSDL2_mixer.dll.a`, `SDL2_mixer.dll` |
| **OpenCL** | Opcional (`WITH_OPENCL`, default OFF; **muy recomendable**) | `trecnis/third_party/CL/` (headers Khronos) + `trecnis/lib/libOpenCL.a` (generada con `gendef`+`dlltool` sobre `C:\Windows\System32\OpenCL.dll` del driver NVIDIA) | `-DWITH_OPENCL=ON` | headers `CL/*.h`, `libOpenCL.a`. Runtime: `OpenCL.dll` del driver (ya instalado) |
| **Embree 4** | Opcional (`WITH_EMBREE`, default ON; degrada) | **no colocada.** | `-DWITH_EMBREE=ON` + colocar `third_party/embree4/` + `lib/embree4.lib` + `bin/{embree4,tbb12,tbbmalloc}.dll` | `embree4/rtcore.h` etc., `embree4.lib` (build MSVC — riesgo con MinGW), 3 DLLs |
| **TBB** | Solo si Embree | dentro del zip de Embree | via Embree | `tbb.lib`, `tbb12.dll`, `tbbmalloc.dll` |
| **DLLs runtime MinGW** | Sí (Windows) | `trecnis/bin/{libstdc++-6,libgcc_s_seh-1,libwinpthread-1}.dll` copiadas de `C:\mingw64\...\bin` | CMake las copia junto al exe (línea 155, ya parcheada) | las 3 DLLs |
| **stb_image** | Sí (vendored) | `src/io/stb_image.{hpp,cpp}` | ninguna, compila con el proyecto | — |
| **Python 3.13** (solo notebook) | No para el juego | `%LOCALAPPDATA%\Programs\Python\Python313` | — | numpy/pandas/matplotlib/tqdm (ya instalados) |

> **Nota sobre el `.lib` de Embree y MinGW:** `embree4.lib` es una import library
> MSVC. MinGW *suele* poder enlazar contra ella para una API C (Embree lo es),
> pero es la fuente de fricción #1 si se activa. **Arrancar con `WITH_EMBREE=OFF`.**

---

## 3. Assets — inventario y rutas que se rompen al mover

### Assets que trecnis **carga de verdad** (hardcodeados en `sr_game.cpp:199-204`)
```
res/textures/skybox3/null_plainsky512_rt.png     ← 6 caras del cubemap del cielo
res/textures/skybox3/null_plainsky512_bk.png        (256×256 tras el reescalado)
res/textures/skybox3/null_plainsky512_ft.png
res/textures/skybox3/null_plainsky512_lf.png
res/textures/skybox3/null_plainsky512_up.png
res/textures/skybox3/null_plainsky512_dn.png
```
**Eso es TODO lo que trecnis lee.** El personaje es procedural (sin assets), no
hay música, no hay OBJ.

### Assets presentes en `trecnis/res/` pero **NO usados** por el código
```
res/cornell/CornellBox-Original.mtl         (falta el .obj)
res/data/{cam_anim_2.txt, skin.anim, skin.weights}
res/mall/{mall.mtl, tex00..tex24 *.png}     (falta mall.obj)
res/music/specialist.ogg
res/robloxian/{robloxian.mtl, robloxian.png}  (falta robloxian.obj)
```
Son restos de las escenas de raytrec. Para *PingPong RT* **no copiar** salvo que
se retome el jugador riggeado (fase 9): entonces harían falta `robloxian.obj`
(está en `raytrec/res/robloxian/`), `skin.weights`, `skin.anim`.

### Rutas que se rompen al mover a otra carpeta
| Ruta | Dónde | Riesgo |
|---|---|---|
| `res/textures/skybox3/*.png` ×6 | `sr_game.cpp:199-204`, strings literales | **Alto.** Si el nuevo layout usa `assets/`, cambiar los 6 strings |
| `res/...` en general | `load_png_texture` abre con `std::ifstream` **relativo al CWD** | El `.exe` DEBE ejecutarse desde la raíz del repo (o usar el target `run` que hace `WORKING_DIRECTORY`) |
| `third_party/` `lib/` `bin/` | `CMAKE_SOURCE_DIR` en `CMakeLists.txt` | Se resuelven bien si van dentro del nuevo repo. **No** son absolutas |
| ninguna ruta absoluta | — | El código no tiene `C:\...` hardcodeado |

**Recomendación para *PingPong RT*:** mantener el nombre `res/` (menos strings que
cambiar) **o** hacer un solo `#define ASSET_DIR "assets/"` y un helper
`asset_path(name)`. El agente de Integración decide; documentarlo.

---

## 4. CMake propuesto para `pingpong-rt/` (NO implementar aún)

Cambios respecto al de trecnis, mínimos y justificados:

```cmake
cmake_minimum_required(VERSION 3.16)
project(pingpong_rt LANGUAGES CXX)          # (1) nombre nuevo

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# --- SDL2 / SDL2_mixer / OpenCL / Embree: IDÉNTICO a trecnis (bloques 13-135) ---
# (copiar tal cual; solo cambia ${PROJECT_NAME})

# (2) fuentes: igual, GLOB_RECURSE src/*.cpp
file(GLOB_RECURSE APP_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/*.cpp")
add_executable(${PROJECT_NAME} ${APP_SOURCES})
target_include_directories(${PROJECT_NAME} PRIVATE "${CMAKE_SOURCE_DIR}/src")

# (3) flags: -march=native detrás de una opción (portabilidad del binario)
option(NATIVE_ARCH "Optimize for the build machine's CPU (non-portable binary)" ON)
target_compile_options(${PROJECT_NAME} PRIVATE -ffast-math)
if(NATIVE_ARCH AND NOT MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE -march=native)
endif()

# (4) opción para el directorio de assets si se elige "assets/" en vez de "res/"
#     target_compile_definitions(${PROJECT_NAME} PRIVATE ASSET_DIR="assets/")

# (5) DLLs runtime Windows + target run: IDÉNTICO a trecnis (bloques 153-185)
```

Resumen: **el 90% del CMake se copia sin cambios.** Los únicos cambios reales son
el nombre del proyecto y poner `-march=native` tras una opción.

### Targets, includes, libs, opciones (resumen para el agente)
- **Target:** `pingpong_rt` (ejecutable único).
- **Include dirs:** `src/` (privado) + `third_party/` cuando `WITH_OPENCL`/`WITH_EMBREE`.
- **Link:** `SDL2::SDL2`, `SDL2_mixer::SDL2_mixer`, `SDL2::SDL2main`, `mingw32`,
  `psapi`, `${OCL_LIB}` (opt), `embree4::embree4` (opt).
- **Opciones:** `WITH_EMBREE` (ON), `WITH_OPENCL` (OFF), `WIN32_GUI` (OFF),
  `NATIVE_ARCH` (nueva, ON).
- **Defines:** `WITH_EMBREE`, `ENABLE_OPENCL` (condicionales).

---

## 5. Comandos de build (para el nuevo proyecto, una vez copiado)

```powershell
# CMake al PATH (cada terminal nueva)
$env:Path = "C:\Program Files\JetBrains\CLion 2024.1\bin\cmake\win\x64\bin;C:\Program Files\JetBrains\CLion 2024.1\bin\ninja\win\x64;" + $env:Path

cd C:\ruta\a\pingpong-rt
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DWITH_EMBREE=OFF -DWITH_OPENCL=ON
cmake --build build -j4        # -j4, NUNCA -j12 en la máquina de 8 GB
build\bin\pingpong_rt.exe --width 960 --height 540 --threads -1
```

- Si `cmake -S . -B build` falla con `Permission denied ... a.exe` → Windows
  Defender escaneando; **relanzar el mismo comando**.
- Alternativa: abrir la carpeta en **CLion** (detecta el CMake y el toolchain MinGW).

---

## 6. Qué copiar de `third_party/` `lib/` `bin/` al nuevo repo

Copiar **tal cual** de `trecnis/` a `pingpong-rt/` (no están en git, hay que
llevarlas a mano):

```
pingpong-rt/
  third_party/
    SDL2/            (91 headers SDL*.h + SDL_mixer.h)
    CL/              (headers Khronos OpenCL)          ← si WITH_OPENCL
    embree4/         (headers)                          ← solo si se activa Embree
  lib/
    libSDL2.dll.a  libSDL2.a  libSDL2main.a
    libSDL2_mixer.dll.a  libSDL2_mixer.a
    libOpenCL.a                                         ← si WITH_OPENCL
  bin/
    SDL2.dll  SDL2_mixer.dll
    libstdc++-6.dll  libgcc_s_seh-1.dll  libwinpthread-1.dll
```

`.gitignore` (copiar de trecnis): ignora `third_party/ build/ bin/ out/ *.o *.obj
*.a *.lib *.dll *.exe *.so *.dylib` + IDE dirs.

---

## 7. Estado del entorno (verificado 2026-09)

| Item | Estado |
|---|---|
| GCC/G++ MinGW 11.1.0 | ✅ en PATH |
| CMake | ⚠️ solo dentro de CLion (3.28.1) |
| Ninja | ⚠️ solo dentro de CLion |
| SDL2 2.32.10 + SDL2_mixer 2.8.2 (MinGW) | ✅ colocados en `trecnis/` **y** `raytrec/` |
| OpenCL (headers + `libOpenCL.a`) | ✅ colocados en `trecnis/` **y** `raytrec/`; GPU verificada (RTX 3050) |
| Embree | ❌ no colocado |
| LaTeX (`pdflatex`) | ❌ (solo para regenerar PDFs de raytrec/docs) |
| Python 3.13 (notebook) | ✅ numpy/pandas/matplotlib/tqdm/ipykernel |
| RAM | 8 GB → compilar con `-j4`; el ray tracer CPU es compute-bound (la GPU con `[G]` es el gran acelerador) |
