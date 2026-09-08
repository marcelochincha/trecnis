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

Phase 0: project migration and renderer validation.

Do not implement advanced physics, spin, AI, scoring or complex animation
until the MVP architecture is working.