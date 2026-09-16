# res/models/racket.obj — needed asset

No 3D racket model exists in this repo yet (checked: no `.obj`/`.mtl`/`.glb`
anywhere in the project). The game already looks for one here and falls back
to the current primitive box paddle if it's missing — nothing is broken by
its absence, but the visual upgrade this checkpoint set up is inert until a
real file is placed at this path.

## Format: OBJ (+ optional .mtl)

Chosen because it's the **only mesh-import format this project already
supports** — `src/engine/assets/obj_loader.{hpp,cpp}` (`load_obj_tris` /
`load_obj_mesh`) was migrated from the base engine but never wired to
anything until now. No glTF/GLB/FBX importer exists and none was added:
pulling one in would mean a new third-party dependency for a single asset,
which the checkpoint explicitly asked not to do without strong justification.

## Required local-space convention

The loader (`Racket::append_local_tris_from_obj`) auto-centres the model on
its own bounding box and uniformly rescales it to match the racket's
configured size, so **exact units/scale/centring don't matter**. What does
matter is axis orientation — author the model so that, before any scaling:

- **+Z** is the blade's hitting face (the side that faces the ball) —
  matches `Racket::face_normal()`.
- **+Y** is "up", toward the head of the paddle (handle end toward −Y).
- **+X** is lateral (blade width).

These are the same local axes the current primitive-box paddle already uses
(`Racket::configure`'s `size = (width, height, thickness)`), so a correctly
authored model drops in with the exact same orientation logic, no extra
per-asset transform needed.

## What the pipeline gives you for free

- Per-vertex smooth normals (computed by `load_obj_tris` regardless of
  whether the OBJ has its own `vn` lines).
- UVs + a diffuse texture, if the `.mtl` has a `map_Kd` (PNG).
- Basic material params from the `.mtl` (`Kd`→albedo, `Pr`/`Ns`→roughness,
  `Pm`→metallic, `Ni`→ior, `Ke`→emission). No `.mtl` at all is fine too —
  it falls back to a flat default albedo.
- These feed the CPU/OpenCL/Embree ray tracers directly through the same
  dynamic-BVH triangle list the current box already uses. The flat-shaded
  RASTER backend only ever uses a single flat colour for the whole racket
  (same as the table/ball already do there), so texture/material detail
  only shows up on the ray-traced backends — expected, not a bug.

## Suggested budget

A few hundred triangles is plenty for a paddle at this camera distance —
low enough that the per-frame dynamic-BVH rebuild cost stays negligible
next to the ball. Not a hard limit, just a sanity target.
