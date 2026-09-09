# PingPong RT

## Project

C++17 real-time table tennis game using our custom hardware-agnostic
ray tracing renderer.

The project is based on the previous raytrec/trecnis projects.

## Goal

Initial MVP:

- One 3D player
- One racket
- One ball
- One wall
- Player hits the ball against the wall
- Ball rebounds
- Real-time ray tracing
- Custom C++ game logic

Do NOT implement a second opponent yet.

The MVP is the final target of the initial development stage.
Implement it incrementally through checkpoints.

## Architecture

Game logic must remain independent from rendering.

Game
  ↓
RenderScene
  ↓
Renderer
  ↓
CPU BVH / OpenCL / Embree / Raster

The game must never implement ray tracing logic.

The renderer must never contain gameplay rules.

## Existing renderer

The existing renderer, BVH and rendering backends are inherited
from the previous projects.

Do not rewrite or replace them unless explicitly requested.

## Development rules

1. Inspect existing code before modifying it.
2. Never invent APIs that do not exist.
3. Prefer adapting existing code over rewriting it.
4. Keep changes small and isolated.
5. Compile after meaningful changes.
6. Run relevant tests after changes.
7. Do not modify unrelated modules.
8. Keep commits small and descriptive.

## Documentation

Detailed architecture information is available in:

docs/ARCHITECTURE.md
docs/CODE_INVENTORY.md
docs/MIGRATION_PLAN.md
docs/AGENT_HANDOFF.md
docs/RENDERER_API.md
docs/BVH_GUIDE.md
docs/BUILD_GUIDE.md

Read only the document relevant to the current task.

## Current development phase


Phase 1: dynamic ball prototype.

The renderer migration and initial renderer validation are complete.

Current checkpoint:
- Dynamic 3D ball
- Delta-time movement
- Arena collision and rebound
- Dynamic BVH
- CPU / OpenCL / Raster rendering

Do not repeat the migration or renderer audit unless explicitly requested.


## Execution limits

Do not enter prolonged trial-and-error loops.

If a build, test, runtime execution, or validation fails:
1. Diagnose the failure.
2. Attempt at most 2 reasonable fixes.
3. If it still fails, stop and report the failure.
4. Do not make speculative changes.
5. Do not run prolonged benchmarks or repeated validation unless explicitly requested.

Never spend extended time trying to force a test to pass. A clear failure report is preferable to uncontrolled changes.


## Checkpoint discipline

Each checkpoint must follow:

Implement
→ Build
→ Short functional test
→ Validate
→ Commit
→ Stop

If validation fails:

Implement
→ Build
→ Test
→ Diagnose
→ Max 2 reasonable fixes
→ Stop if unresolved

## Renderer protection

The existing renderer and BVH are considered stable infrastructure.

Do not modify:
- src/render/**
- bvh.*
- core renderer infrastructure

unless the current task explicitly requires it.

If a problem appears to originate in the existing renderer:
1. Diagnose it.
2. Report the suspected cause.
3. Do not rewrite or refactor the renderer as a speculative fix.