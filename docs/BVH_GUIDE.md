# BVH_GUIDE.md — el BVH y cómo usarlo en *PingPong RT*

> Fuente: `trecnis/src/render/raytrace/bvh.{hpp,cpp}` (476 líneas .cpp) +
> `bvh_accel.cpp` + `accel.hpp`. **Regla del enunciado: no reemplazar este BVH.**
> Este documento explica cómo usarlo sin tocarlo.

---

## 1. Estructuras de datos

### `bvh::Tri` (bvh.hpp:18) — triángulo sombreable (geometría + material)
```cpp
struct Tri {
    vec3  v0, v1, v2;
    vec3  normal;                 // normal de cara (se usa si !smooth)
    vec3  albedo;                 // [0,1]
    float roughness = 0.5f;       // 0 = espejo, 1 = mate
    float metallic  = 0.0f;       // 0 = dieléctrico, 1 = metal
    float ior       = 1.5f;       // (sin uso hasta que haya refracción)
    bool  smooth    = false;      // true → interpola n0/n1/n2
    vec3  n0, n1, n2;             // normales por vértice
    vec3  emission  = {0,0,0};    // radiancia emisiva HDR (>0 → es una luz)
    vec2  uv0, uv1, uv2;          // UVs por vértice
    const texture* tex = nullptr; // null → usar albedo
};
```

### `bvh::TriISect` (bvh.hpp:41) — copia cache-hot para el bucle de hoja
```cpp
struct TriISect { vec3 v0, e1, e2; };   // v0 + dos aristas precomputadas (36 bytes)
```
Se rellena en `build()` paralelo a `tris_`. El traversal solo toca esto; la `Tri`
"gorda" se lee una vez, tras el hit, para sombrear.

### `bvh::Hit` (bvh.hpp:44)
```cpp
struct Hit { float t = 1e30f; int tri = -1; };   // t se puede pre-fijar para acotar
```

### `bvh::BVH::Node` (bvh.hpp:93, privado)
```cpp
struct Node {
    AABB bounds;          // sr_geometry.hpp: { vec3 min, max }
    int  left  = -1;      // interior: índice hijo izq; hoja: -1
    int  right = -1;      // interior: índice hijo der
    int  start = 0;       // hoja: primer índice de triángulo
    int  count = 0;       // hoja: nº de triángulos
};
```
Árbol como `std::vector<Node>` (`nodes_[0]` = raíz). Los triángulos se
**reordenan** dentro de `tris_` para que cada hoja posea un rango contiguo.

### Miembros de `class BVH`
```cpp
std::vector<Tri>      tris_;        // triángulos (reordenados)
std::vector<TriISect> tri_isect_;   // paralelo a tris_, cache-hot
std::vector<Node>     nodes_;
std::vector<vec3>     centroids_;   // solo durante build (se limpia al final)
BuildStrategy         strategy_;
static constexpr int  MAX_DEPTH = 60;   // tope de profundidad (pila fija de traversal)
static constexpr int  MAX_LEAF  = 8;    // tope de tamaño de hoja para el SAH
```

---

## 2. API pública (bvh.hpp:54)

```cpp
void build(std::vector<Tri> tris, BuildStrategy strategy = SAH);   // consume/move el vector
BuildStrategy strategy() const;

bool intersect(const vec3& origin, const vec3& dir, Hit& out) const;   // hit más cercano
bool occluded (const vec3& origin, const vec3& dir, float max_t) const; // any-hit (sombras)

const Tri&  tri(int i)       const;
std::size_t triangle_count() const;
std::size_t node_count()     const;
bool        empty()          const;

void flatten(std::vector<float>& node_bounds,     // export GPU: 8 floats/nodo
             std::vector<int>&   node_links,      //             4 ints/nodo (left,right,start,count)
             std::vector<float>& tris) const;     //             32 floats/tri

struct DebugNode { AABB bounds; int depth; bool leaf; };
void debug_nodes(std::vector<DebugNode>& out) const;   // para el overlay wireframe [V]
```

`enum BuildStrategy { SAH = 0, Median = 1, Morton = 2 };`

---

## 3. AABB

`struct AABB { vec3 min, max; }` vive en `core/sr_geometry.hpp` (no en el BVH), así
lo comparte todo el motor. Helpers **privados** dentro de `bvh.cpp` (no exportados):
`aabb_empty()`, `aabb_grow(b, p)`, `aabb_union(a, b)`, `aabb_area(b)` (= media SA),
`tri_bounds(t)`, y el slab test `aabb_hit(b, o, inv, t_max, &t_near)` (branchless,
min/max, sin `swap` por eje).

> **Para colisiones** (`CollisionSystem`) probablemente quieras estos helpers de
> AABB. **No los saques de `bvh.cpp`** (romperías la regla de no tocar el BVH).
> Cópialos a un `math/sr_aabb.hpp` nuevo o reimplementa los 4 (son triviales).

---

## 4. Construcción

`build(tris, strategy)` (bvh.cpp:100):
1. `tris_ = move(tris)`; calcula `centroids_[i] = (v0+v1+v2)/3`.
2. Si `Morton`: ordena **globalmente** los triángulos por el código Z-order
   (30-bit, `morton3d`) del centroide normalizado a `[0,1]³`.
3. `build_node(0, N, 0)` recursivo top-down.
4. Rellena `tri_isect_` (v0 + aristas) paralelo al orden final.

`build_node(start, count, depth)` (bvh.cpp:195):
- **Hoja** si `count <= 2` **o** `depth >= MAX_DEPTH`.
- Elige eje = dimensión más larga de la caja de centroides.
- Según `strategy_`:
  - **Morton**: `mid = start + count/2` (ya ordenado globalmente → split por índice
    preserva localidad). Build O(N log N) por el sort, recursión O(N).
  - **Median**: `partition_median` (quickselect sobre `centroids_[axis]`).
  - **SAH** (por defecto): 12 bins sobre el eje; barrido para área·conteo
    izquierda/derecha de cada uno de los 11 planos; coste
    `left_area·left_cnt + right_area·right_cnt`; se compara contra el coste de
    hacer hoja (`= count`). Si el SAH decide "no dividir" pero `count > MAX_LEAF`,
    **fuerza un split mediano** (evita que un quad de suelo gigante colapse cientos
    de triángulos en una hoja). Partición in-place con `partition()`.
- Recursión: `build_node(start, mid-start)` y `build_node(mid, ...)`.
- ⚠️ `nodes_` puede realocar durante la recursión → siempre indexa por `idx`, no
  por referencia guardada.

### Coste medido (raytrec `--bench`, escena "Large" ~205k tri, i5-11400H)
| Estrategia | build | nodos/rayo | uso |
|---|---|---|---|
| SAH | ~188 ms | ~137 | árbol estático (build 1 vez) |
| Median | ~144 ms | ~230 | — |
| Morton | ~117 ms | ~450–660 | árbol dinámico (build cada frame) |

Para *PingPong RT* el BVH dinámico tendrá **cientos–pocos miles** de triángulos
(pelota + raqueta + jugador), no 200k. Un rebuild Morton de eso es **< 1 ms**.

---

## 5. Traversal

### `intersect` (bvh.cpp:326) — hit más cercano
- Pila fija `int stack[64]` + `float stack_t[64]` (distancia de entrada).
- **Front-to-back ordenado**: en cada nodo interior testea **ambas** cajas hijas,
  desciende de inmediato a la más cercana y **apila** solo la lejana con su
  `t_near`. Al popear, descarta nodos cuyo `stack_t >= best` (ya culleados).
- Hoja: `tri_hit(tri_isect_[i], o, d, &t)` (Möller–Trumbore compacto) para cada
  triángulo del rango; actualiza `best`/`best_tri`.
- `EPS = 1e-8f`; `t > EPS` (no hay hit a distancia 0).
- **No hace backface culling** (coherente con `render/`: la normal de sombreado se
  voltea hacia el rayo).

### `occluded` (bvh.cpp:385) — any-hit para sombras
- Pila simple `int stack[64]`. Devuelve `true` en cuanto un triángulo golpea a
  `t < max_t`. No busca el más cercano.
- Guard `sp + 2 <= 64` antes de apilar los dos hijos.

**La pila de 64 + `MAX_DEPTH = 60`** garantizan que nunca desborda.

---

## 6. Geometría estática vs dinámica — el patrón de dos árboles

`BvhAccel : ISceneAccel` (bvh_accel.cpp) es lo que ve `trace_ray`:

```cpp
bool BvhAccel::intersect(o, d, out) {
    float best = 1e30f;
    if (use_bvh) {
        if (dynamic_bvh) { Hit h; if (dynamic_bvh->intersect(o,d,h)) { best=h.t; out={h.t,&dynamic_bvh->tri(h.tri)}; } }
    } else if (brute_tris) { /* bucle Möller–Trumbore sobre brute_tris */ }
    if (static_bvh && !static_bvh->empty()) {
        Hit h; h.t = best;                       // ← acota con el hit dinámico
        if (static_bvh->intersect(o,d,h)) { out = {h.t, &static_bvh->tri(h.tri)}; }
    }
    return hit;
}
occluded(o,d,max_t) = static_bvh->occluded(...) || dynamic_bvh->occluded(...)
```

Un `Hit` con `out.t` prefijado acota la búsqueda: el estático se consulta con
`h.t = best` para no encontrar nada más lejos que el hit dinámico.

---

## 7. Cómo debe usar *PingPong RT* el BVH

| Objeto | Árbol | Frecuencia de build | Estrategia | Cómo |
|---|---|---|---|---|
| **Suelo** | estático | 1 vez (`game_rebuild_static`) | SAH | `add_box(tris, lo, hi, albedo, rough)` |
| **Pared** | estático | 1 vez | SAH | `add_box` (un cuboide fino vertical) |
| **Mesa, red** (futuro) | estático | 1 vez | SAH | `add_box` / `load_obj_tris` |
| **Pelota** | **dinámico** | cada frame | Morton | generar los tris de la esfera en `ball.pos` (con `add_sphere` de raytrec) y `push_back` en `rt_tris` |
| **Raqueta** | **dinámico** | cada frame | Morton | `fold_mesh(rt_tris, racket.mesh, ...)` con su `modelMatrix` actualizado |
| **Jugador** | **dinámico** | cada frame | Morton | `skin.apply(t)` → `fold_mesh(rt_tris, player.mesh, ...)` |

El bucle es el `build_dynamic` que ya existe (sr_game.cpp:97), solo hay que
añadir objetos antes del `dynamic_bvh.build`.

### Esfera de baja resolución para la pelota
`raytrec/src/game/sr_scene.cpp:37` `add_sphere(out, center, radius, albedo,
roughness, metallic, ior, slices, stacks, smooth=true)`. Con `slices=12, stacks=8`
son ~192 triángulos. Con `smooth=true` la pelota se ve redonda (normales por
vértice). Copiar esta función a `game/` o `engine/`.

---

## 8. El BVH como motor de colisiones (para `CollisionSystem`)

**El BVH ya sabe responder consultas de rayo contra geometría arbitraria.** Eso es
directamente aprovechable:

| Consulta de colisión | Cómo con el BVH existente |
|---|---|
| ¿La pelota (centro `P`, vel `V`, radio `r`) va a chocar con la pared/suelo/mesa este frame? | `static_bvh->intersect(P, normalize(V), hit)`; si `hit.t <= |V|·dt + r` → impacto. La normal es `hit_tri.normal`. (Aproxima esfera con rayo; suficiente para MVP.) |
| ¿La pelota tocó la raqueta? | Opción A: caja AABB de la raqueta vs esfera (barato). Opción B: `dynamic_bvh->intersect` limitando a los tris de la raqueta. Para MVP, **AABB de la raqueta vs esfera**, sin el BVH. |
| Punto más cercano pelota↔superficie | no lo da el BVH; hacerlo analítico (esfera-plano, esfera-AABB) |

**Recomendación:** para MVP la física es esfera vs planos analíticos (pared =
plano `x = const`, suelo = plano `y = 0`), **sin usar el BVH**. Usar
`static_bvh->intersect` como raycast de "look-ahead" solo si aparecen paredes en
ángulo o geometría irregular. El BVH **no** se toca; se **consulta**.

---

## 9. Debug del BVH

- `debug_nodes(out)` → lista `{AABB, depth, leaf}` por DFS iterativo. `sr_hud.cpp`
  `draw_bvh_debug` la dibuja como wireframe hasta `e->bvh_debug_depth` (12).
  Tecla `[V]`.
- `[N]` `draw_normals_debug` dibuja las normales de `rt_tris` + `static_bvh`.

---

## 10. Limitaciones conocidas (documentar, no arreglar)

1. **Sin refit / update incremental.** El BVH dinámico se reconstruye entero cada
   frame. Aceptable a la escala de *PingPong RT*; si el jugador es un mesh de
   decenas de miles de triángulos, medir.
2. **`intersect` no devuelve las coordenadas baricéntricas ni el punto** — solo
   `t` y el índice. El shader recalcula `P = o + d·t` y las UV/normales suaves con
   `tri_smooth_normal` / `tri_interp_uv` (sr_raytrace.cpp) resolviendo el sistema
   2×2. Si `CollisionSystem` necesita el punto exacto de contacto, lo mismo.
3. **`build` mueve el vector de entrada** — si necesitas conservar la lista de
   tris (p. ej. para Embree o brute force), pásala por copia o guárdala aparte
   (así lo hace `Game::rt_tris` + `s.brute_tris`).
4. **Cap `MAX_DEPTH = 60`**: geometría degenerada (muchos centroides coincidentes)
   fuerza hojas gordas, no crash.
