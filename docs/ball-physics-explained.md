# Ball physics — how it actually works

Companion to `ball-physics.md` (which is the address/struct map). This file explains the *mechanics*
of each part in plain terms + reconstructed pseudocode, enough to re-implement. All numeric constants
are the real values read from `main.dol` (Wii build). Coordinates: **Y is up/height, X and Z are the
table plane** (confirmed by the height-vs-table tests using `SampleGetPos+4` = Y).

Pseudocode here is my own reconstruction of the logic, not decompiler output.

---

## 0. The big picture

RTT does **not** simulate the ball every frame with the render loop. Instead, the moment the ball is
struck it **pre-computes the entire future trajectory** in one burst, stores it as a list of
time-stamped samples, and then everything else (rendering, AI, camera, "can I reach it?") just
**looks up or interpolates** that stored trajectory. A light "autoaim" pass then bends the stored
trajectory toward where the striking player intended to aim.

```
racket hit ──> solve outgoing vel/spin ──> ProcessHit:
                                              loop { integrate one step; append sample }
                                              until 2 bounces or 6 table crossings
                                            ──> classify outcome (in/out/net…)
                                            ──> autoaim: bend path toward target
stored trajectory (pongLerpQueue)  ──> everyone reads it by time t
```

Why do it this way: deterministic gameplay (AI knows exactly where the ball will go), cheap per-frame
cost (just interpolation), and a clean place to apply aim-assist without fighting the physics.

---

## 1. The physics step — `IntegrateStep`

One step advances the ball by a small time `dt` (`FUN_8004bca0` supplies it; effectively a fixed
sub-step). It is **semi-implicit Euler**:

```
a   = ComputeAcceleration(vel, spin)     // forces
vel = vel + a   * dt                      // integrate velocity first
pos = pos + vel * dt                      // then position (semi-implicit)
```

### Forces — `ComputeAcceleration`
Three contributions, summed:

```
a_magnus  = k_magnus * (spin × vel)      // cross product → curve/swerve from spin
a_drag    = -k_drag * vel                 // air resistance, opposes motion (only if |vel|² > 1e-6)
a_gravity =  g_vec                         // downward gravity vector (from FUN_80045dfc)
a = a_magnus + a_drag + a_gravity
```

- **Magnus** is the whole reason spin matters: `spin × vel` is perpendicular to both, so topspin dips
  the ball, backspin floats it, sidespin curves it. `k_magnus` comes from `FUN_8011b4d0`.
- There is a **spin gate**: a config angle (`r13-0x7468`) is converted deg→rad with `π/180`
  (`0.0174533`) and if the spin axis is too shallow relative to velocity the Magnus term is zeroed.
  This prevents nonsensical curve at grazing angles.
- Drag is linear in velocity here (not the classic v² model) — cheaper, good enough for a table.

**To replicate:** `a = kM*cross(spin,vel) - kD*vel + g`. Tune `kM` for how much spin bends flight.

---

## 2. Bounce off the table — `ResolveBounceImpulse`

When a step detects the ball crossing the table surface (Y drops below table height, from
`FUN_8011d808`), the bounce is resolved as a **contact impulse that includes spin**. This is what
makes topspin kick forward and backspin check/stop — the important bit for realistic table tennis.

```
// contact-point velocity = linear vel + spin about the contact radius
v_contact = vel + (spin × r_contact)
vn = component of v_contact along the surface normal      // approach speed
vt = tangential part (what causes friction & spin change)

if (vn is approaching):
    // NORMAL impulse — bounce height
    apply normal restitution (table restitution ≈ 0.95, from IntegrateStep's r2-0x7800)
    // TANGENTIAL impulse — stick vs slide
    if (|vt| small vs normal impulse, threshold 0.16):
        static/"sticking" branch  → strong coupling, grips and flips spin↔velocity
    else:
        kinetic friction branch   → friction coeff 0.4, partial slide
    // the tangential impulse also TORQUES the ball:
    spin += (tangential_impulse × r_contact) * spin_coupling(1.5)
    spin *= (1 - spin_decay)   // 0.002 bleed
```

Key constants (exact): restitution **0.95**, kinetic friction **0.4**, sticking/sliding threshold
**0.16**, spin↔impulse coupling **1.5**, spin decay **0.002**.

Physical reading: 0.95 restitution ≈ a ping-pong ball keeps ~95% of vertical speed per bounce (bounces
high). The friction branch is why **incoming topspin bounces into forward speed** and **backspin
kills forward speed** — the tangential contact velocity gets converted between linear and angular.

**To replicate:** the standard "friction + restitution with angular coupling" contact model. If you
only do normal restitution and ignore the tangential/spin term, the ball will feel dead and spin
won't affect the bounce — that tangential branch is the soul of it.

---

## 3. Net collision

Inside the same step, the net is modelled as a **capsule** (a line segment `start`+`axis` with a
`radius` and `length`). The ball's swept position is tested against that capsule; on a hit the sample
is flagged and the debug build prints `Collided with net! ...` with the capsule params. Net threshold
constant **0.25**. Gameplay-wise this ends the rally (ball into the net).

**To replicate:** sphere-vs-capsule test each step; simplest robust net model.

---

## 4. The prediction loop — `ProcessHit`

This is the driver that fills the trajectory:

```
validate post-hit spin magnitude (cap ≈ 1000 rad/s, i.e. |spin|² ≤ 1e6)
seed sample0 = { t=0, pos, vel, spin }
bounces = 0; crossings = 0
loop:
    IntegrateStep()          // advance one dt, resolve bounce/net
    AddSample()              // append the new state to the queue
    if (table bounce happened)      bounces++
    if (ball crossed table plane)   crossings++
    if (bounces >= 2 or crossings >= 6 or sampleCount >= 120) break
classify outcome
reseed spin for the fudge stage
```

So the queue ends up holding the ball's path from the hit until it has bounced twice / crossed the
table six times — i.e. long enough to know where the rally goes, capped at 120 samples.

---

## 5. The trajectory store — `pongLerpQueue`

A fixed ring buffer of **120 samples**, each `0x4c` bytes, **sorted by time `t`** (`t` at sample+0x00).
Three indices: `tail` (write), `head` (read), `count`.

- **Append** (`Enqueue`): write at `tail`, `tail=(tail+1)%120`, `count++` (drops if full).
- **Read at arbitrary time** (`FindNeighborIndices(t)` + lerp): binary/linear-scan for the two
  samples that bracket `t`, then linearly interpolate position/velocity between them. This is how the
  renderer/AI ask "where is the ball at time t?" without storing every frame.
- **Retire old** (`PopExpiredSamples`): as the play clock advances, samples with `t < now` are popped
  off the head. So the queue is a sliding window of the *remaining* future path.

Edge cases are explicitly handled with warnings: empty queue, fewer than 2 samples, `t` before the
first sample (`MinT`) or after the last (`MaxT`).

**To replicate:** a ring buffer of `(t, pos, vel, spin, flags)` samples + a "sample at time t" lerp
lookup. This decouples physics rate from render rate cleanly.

### Sample layout (0x4c bytes)
`+0x00` t · `+0x04` second time field · `+0x08/09/0a` flags (hittable/bounce/reachable) ·
`+0x0c` pos(x,y,z) · `+0x1c` vel(x,y,z) · `+0x2c` spin(x,y,z) · `+0x3c..0x48` extra vec (contact
normal/accel, populated on some paths only).

---

## 6. Which samples can a player hit? — reachability & the "hittable window"

Not every point on the path is reachable by a paddle. Two ideas:

- **Hittable window**: contiguous run of samples flagged hittable (`+0x0a != 0`, `+0x08 == 0`).
  `FindFirstHittableSample` / `FindFirstNonHittableSample` bracket it; `MarkHittableFlag` sets the
  flag along a range based on height.
- **Reach envelope** (`IsSampleReachable`): tests the sample position against the **table
  half-extents** (from `r13-0xb80`: +0x08 = X half-width, +0x10 = Z half-length, +0x0c = height)
  scaled by **0.5**, plus a reach margin **0.05** and tolerance **0.02**, offset by the table height.
  Returns true if the ball at that sample is within paddle reach in X or Z. This is the "can the
  player physically get to it" predicate the AI and aim-assist use.

`FindNearestHittableSample(predictData, refPoint, &idx)` then returns the hittable sample closest
(min squared 3D distance) to a reference point — used to pick *which* point of the arc to aim from.

---

## 7. Where does the ball end up? — outcome classification

After integrating, `ClassifyOutcome` → `ClassifyOutcomeCore` walks the samples to decide the result.
It is **not an integer enum**; it's **bit-flags + an event time** stored in the predict data:
`bit0 = outcome known`, plus type bits, plus the time of the deciding event. It's computed by testing:

- **table-plane crossings** (`FUN_80123378`) — did it land in/out,
- **out-of-table-bounds** (`FUN_80123700`),
- **bounces** (`FUN_80123460`).

If it can't decide, it **extrapolates** an outcome; if still stuck it warns
`Ball prediction outcome unknown!` and forces a default. This is what lets the AI/scoring know
"this shot lands out / lands in / hits the net" the instant the ball is struck.

---

## 8. Aim-assist — how the ball gets nudged toward the target ("fudge")

This is the layer that makes shots feel intentional on a casual controller. It **never rewrites the
physics**; it bends the already-computed trajectory toward the striker's intended target.

```
AutoAimTick(player):
    if no valid target: return
    target   = player.aimTarget          // player+0xd0
    idx      = FindNearestHittableSample(prediction, target)   // closest reachable arc point
    if found:
        ApplyPositionFudge(prediction, target, idx)   // shift sample positions toward target
        ApplyTimestampFudge(prediction, idx)          // adjust WHEN it arrives
        FudgeToTarget()                                // propagate
```

- **ApplyPositionFudge** computes `delta = target - samplePos` and distributes that offset across the
  samples, interpolated along the path (more correction near the target, tapering away), then
  **recomputes velocities** so the bent path stays physically continuous.
- **ApplyTimestampFudge** does the same for the time axis (arrival timing).
- **How much** it may bend is per-stroke, from `ballfudge.xml`:

  | stroke | max nudge | rate/s |
  |---|---|---|
  | SOT (smash-over-table) | **0.45** | 0.60 |
  | Scoop | 0.40 | 0.45 |
  | Slam, LungeFar | 0.30 | 0.40/0.30 |
  | OTT | 0.25 | 0.60 |
  | normal FH/BH, soft, push, JIT | 0.20 | 0.10 |

  `MinFudgeDist` is always 0.1 (a small always-on assist); `MaxFudgeDist` caps the reach of the
  assist; `DistPerSecond` limits how fast the correction is applied so it isn't a visible snap.

Reading: aggressive placed shots (SOT/Scoop) get the most aim help; soft/defensive returns get the
least. The aim target itself (`player+0xd0`) is written upstream by the AI/input layer (see the input
subsystem) — this module only consumes it.

**To replicate:** keep physics pure; add a post-pass that, given an intended target and a per-shot
max-distance budget, offsets the predicted samples toward the target and re-derives velocities.

---

## 9. The strike itself — `OnRacketHit` + `SolveHitAim`

- **`OnRacketHit`** resolves which of the **19 hit zones** the contact is in and the **shot type**
  (`hitState+0xb4`), then builds the outgoing position/velocity/spin and calls `ProcessHit`. A
  scripted/replay flag (`0x1000`) bypasses the solve and uses pre-baked vectors.
- **`SolveHitAim`** computes the outgoing velocity/spin needed to send the ball toward the desired
  landing point: it **samples two candidate arcs** (high/low, `FUN_80048c9c` filling 416-float arc
  tables) and picks between them by spin, honoring the shot type. It writes the chosen launch
  velocity back for `ProcessHit` to integrate.

---

## 10. Constants cheat-sheet (real values, from `main.dol`)

| quantity | value | where |
|---|---|---|
| table bounce restitution | 0.95 | bounce normal impulse |
| kinetic friction | 0.4 | bounce tangential (sliding) |
| sticking/sliding threshold | 0.16 | bounce branch select |
| spin↔impulse coupling | 1.5 | bounce angular update |
| spin decay per bounce | 0.002 | bounce |
| spin cap | ~1000 rad/s (|spin|²≤1e6) | ProcessHit validate |
| net collision threshold | 0.25 | net capsule test |
| table half-extent scale | 0.5 | reachability |
| paddle reach margin | 0.05 | reachability |
| reach tolerance | 0.02 | reachability |
| deg→rad | 0.0174533 (π/180) | spin gates |
| aim-assist max nudge | 0.20–0.45 per stroke | ballfudge.xml |

Gravity and Magnus coefficients are supplied by functions (`FUN_80045dfc`, `FUN_8011b4d0`), not flat
constants — read those if you need the exact numbers.

---
*Last updated: 2026-09-06 · companion to `ball-physics.md`*
