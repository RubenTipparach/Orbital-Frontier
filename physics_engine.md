# Physics Engine — N-Body Orbital Mechanics

Technical reference for the Orbital-Frontier physics system: gravitational simulation,
numerical integration, GPU compute, and multi-threaded architecture.
Source engine: [Caelum](https://github.com/RubenTipparach/Caelum) (`hex-planets/`).

---

## 1. Current Orbital Mechanics (Caelum Baseline)

### Kepler Solver

`Caelum/src/celestial.c` implements classical two-body Keplerian orbits:

1. Advance elapsed time by `dt`
2. Compute mean anomaly: `M = (2π / period) × t`
3. Solve Kepler's equation `M = E - e·sin(E)` via 5 Newton-Raphson iterations
4. Convert eccentric anomaly → true anomaly → position in perifocal frame
5. Rotate perifocal → inertial via inclination, ascending node, argument of periapsis

`solar_system_update()` calls `kepler_position()` for every moon each frame.

### Orbital Parameters

`OrbitParams` struct in `Caelum/src/celestial.h`:

| Field            | Example Range      | Description                 |
|------------------|--------------------|-----------------------------|
| semi_major_axis  | 2,000–13,000 km    | Orbit size                  |
| eccentricity     | 0.02–0.09          | Orbit shape                 |
| inclination      | 5–40°              | Tilt relative to equator    |
| period           | 1–16.5 hours       | Orbital period              |

### Gravity Detection

`solar_system_find_gravity_body()` checks surface altitude to each body within
`MOON_SOI_RADIUS` (50km sphere of influence). Returns the closest body — used to
determine which gravity well the player is in and which "up" direction applies.

### CelestialBody Struct

```c
typedef struct {
    char         name[32];
    double       pos_d[3];          // double-precision world position
    float        radius;            // body radius (meters)
    float        surface_gravity;   // m/s² at surface
    OrbitParams  orbit;             // Keplerian elements
    ShapeParams  shape;             // ellipsoid/capsule geometry
    sg_buffer    vbuf, ibuf;        // GPU mesh
} CelestialBody;
```

---

## 2. N-Body Physics Extension

### Why N-Body?

Kepler orbits are exact solutions to the **two-body** problem. They cannot model:
- Moon–moon gravitational perturbations
- Lagrange point dynamics (L1–L5 equilibria)
- Chaotic orbital captures and resonances
- Tidal interactions between closely orbiting bodies

N-body simulation computes gravitational forces between all pairs, enabling these effects.

### Direct Summation — O(n²)

Every body accumulates force from every other body:

```
for each body i:
    F_i = 0
    for each body j ≠ i:
        r = pos_j - pos_i
        F_i += G * m_i * m_j * r / (|r|² + ε²)^(3/2)
```

- Softening parameter `ε` prevents singularity at close approach
- Simple, exact, trivially parallelizable on GPU
- Practical for ≤ 1,000 bodies

### Barnes-Hut — O(n log n)

Spatial octree partitioning with multipole approximation:

1. **Build octree**: recursively subdivide space, insert bodies into leaf nodes
2. **Compute centers of mass**: bottom-up traversal, each node stores total mass + CoM
3. **Force evaluation**: for each body, walk tree — if node angular size `s/d < θ`,
   treat entire subtree as single mass at CoM; otherwise recurse into children

- Accuracy parameter `θ ≈ 0.5` (lower = more accurate, slower)
- Better for large body counts (1,000+)
- Harder to parallelize (tree traversal is pointer-chasing)

### Hybrid Approach

- **Nearby pairs** (within cutoff radius): direct summation for accuracy
- **Distant clusters**: Barnes-Hut approximation
- Cutoff tuned per simulation — balances accuracy and performance

### Comparison

| Method          | Complexity  | GPU-Friendly | Accuracy | Best For        |
|-----------------|-------------|--------------|----------|-----------------|
| Direct Sum      | O(n²)       | Excellent    | Exact    | ≤1,000 bodies   |
| Barnes-Hut      | O(n log n)  | Moderate     | Approx   | 1,000+ bodies   |
| Kepler (current)| O(n)        | N/A          | 2-body   | Fixed orbits    |

---

## 3. RK4 Integration

### Why RK4?

| Integrator | Order | Error/Step | Stability      | Energy Drift     |
|------------|-------|------------|----------------|------------------|
| Euler      | 1st   | O(dt)      | Unstable       | Grows fast       |
| Verlet     | 2nd   | O(dt²)     | Symplectic     | Bounded (long-term) |
| RK4        | 4th   | O(dt⁴)     | Non-symplectic | Slow drift       |

RK4 provides the best short-to-medium term accuracy. For very long integrations
(millions of orbits), consider symplectic Verlet/leapfrog to preserve energy.

### The Algorithm

State vector: `S = {position, velocity}` for each body.
Derivative function: `dS/dt = {velocity, acceleration(position)}`.

```
k1 = dt * f(t,        S)
k2 = dt * f(t + dt/2, S + k1/2)
k3 = dt * f(t + dt/2, S + k2/2)
k4 = dt * f(t + dt,   S + k3)

S_next = S + (k1 + 2*k2 + 2*k3 + k4) / 6
```

Each `k` evaluation requires a full force computation (n-body or Barnes-Hut).
RK4 costs 4× per step but allows ~16× larger timesteps than Euler for equal accuracy.

### Fixed Timestep Accumulator

From Gaffer on Games "Fix Your Timestep!":

```c
double accumulator = 0.0;
const double PHYS_DT = 1.0 / 60.0;  // 60 Hz physics

void update(double frame_dt) {
    accumulator += frame_dt;
    while (accumulator >= PHYS_DT) {
        integrate_rk4(state, PHYS_DT);
        accumulator -= PHYS_DT;
    }
    double alpha = accumulator / PHYS_DT;
    render_state = lerp(prev_state, state, alpha);  // interpolate remainder
}
```

Fixed timestep ensures deterministic, reproducible physics regardless of frame rate.

### Energy Monitoring

RK4 is not symplectic — total orbital energy will drift over time. Monitor:

```c
double E = 0.5 * m * v² - G * M * m / r;  // kinetic + potential
```

If drift exceeds threshold, reduce `PHYS_DT` or switch to symplectic integrator.

---

## 4. Compute Shaders for N-Body

### Sokol Compute Support

As of March 2025, `sokol_gfx.h` natively supports compute shaders:
- `sg_make_pipeline()` for compute pipelines (no render state)
- `sg_dispatch()` inside compute passes
- Storage buffers via `sg_make_buffer()` with `SG_BUFFERTYPE_STORAGEBUFFER`
- Cross-platform: D3D11, Metal, WebGPU (GL fallback to CPU)

### Tile-Based N-Body Kernel

The classic GPU n-body approach (GPU Gems 3, Ch. 31):

```
Buffer A: [pos.xyz, mass] × N bodies  (read)
Buffer B: [vel.xyz, pad]  × N bodies  (read/write)
Buffer C: [pos.xyz, mass] × N bodies  (write — next frame)

Kernel (workgroup_size = 128):
    tid = global_thread_id
    my_pos = A[tid]
    acc = vec3(0)

    for tile in range(0, N, TILE_SIZE):
        // Load tile into shared memory
        shared_pos[local_id] = A[tile + local_id]
        barrier()

        // Accumulate forces from tile
        for j in range(TILE_SIZE):
            r = shared_pos[j].xyz - my_pos.xyz
            dist_sq = dot(r, r) + softening²
            acc += shared_pos[j].w * r / (dist_sq * sqrt(dist_sq))
        barrier()

    B[tid].xyz += acc * G * dt
    C[tid].xyz  = my_pos.xyz + B[tid].xyz * dt
```

**Why tiles?** Loading N bodies into shared memory reduces global memory traffic by
`N / TILE_SIZE` — the dominant optimization for memory-bound n-body.

### Workgroup Sizing

| Backend | Recommended | Shared Memory              |
|---------|-------------|----------------------------|
| D3D11   | 128–256     | UAV (Unordered Access View) |
| Metal   | 128–256     | `threadgroup` memory        |
| WebGPU  | 64–128      | `workgroup` storage class   |

Dispatch count: `ceil(N / workgroup_size)`.

### Double Buffering

Two position SSBOs ping-pong each frame:
- Frame N: read from buffer A, write to buffer C
- Frame N+1: read from buffer C, write to buffer A

Avoids read-write hazards without explicit synchronization.

### Backend-Specific Notes

**D3D11**: compute shader 5.0, storage buffers map to UAVs. Specify `register_u_n`
in sokol shader descriptors.

**Metal**: requires explicit `mtl_threads_per_threadgroup` in `sg_shader_desc`
(not auto-reflected by sokol-shdc). Use `[[threadgroup]]` attribute for shared memory.

**WebGPU/WGSL**: `@compute @workgroup_size(128)` decorator. Storage buffers in
`group(0)` bindings. WGSL uses `var<workgroup>` for shared memory.

**GL fallback**: no compute support in GL 3.3 / GLES3. Fall back to CPU path.

---

## 5. Multi-Threaded CPU Physics

### Existing Job System

`Caelum/src/job_system.h` provides a thread pool:

```c
JobSystem* job_system_create(int num_workers);
void       job_system_submit(js, func, data);     // blocking if queue full
bool       job_system_try_submit(js, func, data);  // non-blocking
int        job_system_pending(js);
void       job_system_flush(js);                   // wait for all jobs
void       job_system_destroy(js);
```

Currently used for LOD mesh generation (4 workers) and erosion computation.

### N-Body Parallelization Strategy

**Spatial partitioning**: reuse Barnes-Hut octree as broad-phase. Assign subtree
force evaluations to different worker threads.

**Per-thread force buffers**: each thread accumulates forces into its own `vec3[]`
array — no atomics, no contention. After all threads complete, main thread reduces:

```c
// Per-thread accumulation (parallel)
void force_worker(int thread_id, int body_start, int body_end) {
    for (int i = body_start; i < body_end; i++) {
        thread_forces[thread_id][i] = compute_force(i);
    }
}

// Deterministic reduction (sequential, fixed order)
for (int i = 0; i < num_bodies; i++) {
    total_force[i] = vec3_zero;
    for (int t = 0; t < num_threads; t++) {
        total_force[i] = vec3_add(total_force[i], thread_forces[t][i]);
    }
}
```

### Load Balancing

- Partition body index ranges evenly across workers
- For Barnes-Hut: partition by octree subtree (uneven body counts per subtree)
- Fine-grained batches (small ranges) keep all threads busy
- Use `job_system_try_submit()` from main thread to avoid blocking the render loop

---

## 6. Determinism & Architecture

### Fixed Timestep (Mandatory)

Variable `dt` breaks reproducibility. The accumulator pattern (Section 3) guarantees
identical physics regardless of frame rate. This is essential for:
- Save/load consistency
- Multiplayer lockstep synchronization (project has `Caelum/src/lobby.c`)
- Replay systems
- Automated testing

### Consistent Accumulation Order

Floating-point addition is **not associative**: `(a + b) + c ≠ a + (b + c)`.
Multi-threaded force accumulation must reduce in a fixed, deterministic order:

1. Each thread writes to its own buffer (indexed by thread ID)
2. Main thread reduces buffers in order `0, 1, 2, ... N-1` — always the same sequence
3. Result is bitwise identical across runs

### Precision Techniques

**Kahan compensated summation** for force totals — tracks running error term:

```c
typedef struct { double sum; double comp; } KahanAccum;

void kahan_add(KahanAccum* a, double val) {
    double y = val - a->comp;
    double t = a->sum + y;
    a->comp = (t - a->sum) - y;
    a->sum = t;
}
```

Reduces rounding error from O(n·ε) to O(ε²) over long simulations.

**Compiler flags**: avoid `-ffast-math` (reorders operations, breaks IEEE 754).
Use `-ffp-contract=off` if strict reproducibility is needed across platforms.

### GPU ↔ CPU Coexistence

| Body Count | Path | Rationale                               |
|------------|------|-----------------------------------------|
| < 50       | CPU  | Overhead of GPU dispatch exceeds benefit |
| ≥ 50       | GPU  | Tile-based compute dominates             |

Both paths produce identical `double pos_d[3]` + `double vel_d[3]` arrays consumed
by the renderer. The threshold is tunable and should be profiled per platform.

### Integration Point

`solar_system_update()` in `Caelum/src/celestial.c` becomes the dispatch point:

```c
void solar_system_update(SolarSystem* ss, double dt) {
    if (ss->n_body_enabled) {
        if (ss->body_count >= GPU_THRESHOLD && gpu_compute_available()) {
            nbody_gpu_step(ss, dt);      // compute shader path
        } else {
            nbody_cpu_step(ss, dt);      // job system path
        }
    } else {
        // Legacy: Kepler solver for unperturbed 2-body orbits
        for (int i = 0; i < ss->body_count; i++) {
            kepler_position(&ss->bodies[i], ss->elapsed_time);
        }
    }
}
```

Kepler solver is kept as a fast-path for stable, unperturbed orbits (menu screens,
distant moons outside perturbation range).

### Render Connection

Body positions feed into the existing rendering pipeline unchanged:
- `CelestialBody.pos_d[]` updated by physics step
- Camera-relative float32 conversion (high/low split) happens in render pass
- LOD retargeting (`lod_tree_retarget()`) unaffected — it only cares about position + radius
- Gravity body detection (`solar_system_find_gravity_body()`) uses updated positions

---

## References

| Topic | Source |
|-------|--------|
| N-body GPU | [GPU Gems 3 Ch. 31, "Fast N-Body Simulation with CUDA"](https://developer.nvidia.com/gpugems/gpugems3/part-v-physics-simulation/chapter-31-fast-n-body-simulation-cuda) |
| Barnes-Hut | [Barnes & Hut 1986, "A hierarchical O(N log N) force-calculation algorithm"](https://doi.org/10.1038/324446a0) |
| Barnes-Hut tutorial | [beltoforion.de Barnes-Hut Galaxy Simulator](https://beltoforion.de/en/barnes-hut-galaxy-simulator/) |
| Fixed timestep | [Gaffer on Games, "Fix Your Timestep!"](https://gafferongames.com/post/fix_your_timestep/) |
| Deterministic lockstep | [Gaffer on Games, "Deterministic Lockstep"](https://gafferongames.com/post/deterministic_lockstep/) |
| RK4 tutorial | [prappleizer, "RK4 Tutorial: N-Body"](https://prappleizer.github.io/Tutorials/RK4/RK4_Tutorial.html) |
| RK4 reference | [Press et al., "Numerical Recipes" Ch. 16](https://numerical.recipes/) |
| Sokol compute | [Sokol compute shader update (Mar 2025)](https://floooh.github.io/2025/03/03/sokol-gfx-compute-update.html) |
| Sokol library | [github.com/floooh/sokol](https://github.com/floooh/sokol) |
| Kahan summation | [Wikipedia: Kahan summation algorithm](https://en.wikipedia.org/wiki/Kahan_summation_algorithm) |
| Symplectic integrators | Hairer, Lubich & Wanner, "Geometric Numerical Integration" |
| Orbital mechanics | [Braeunig, "Orbital Mechanics"](http://www.braeunig.us/space/orbmech.htm) |
| Kepler equation | [Wikipedia: Kepler's equation](https://en.wikipedia.org/wiki/Kepler%27s_equation) |
