# Development Roadmap

Four-phase plan for Orbital-Frontier. Each phase produces a playable milestone.

---

## Phase 1 — 64-Bit Engine & Planet Rendering

**Goal**: Stand on a procedurally generated planet and look around. The foundation
everything else builds on.

### 1.1 Sokol Bootstrap
- [ ] Project scaffold: CMakeLists.txt, sokol headers, sokol-shdc pipeline
- [ ] Window + input via `sokol_app.h`, graphics init via `sokol_gfx.h`
- [ ] Backend targets: D3D11 (Windows), Metal (macOS), GL (Linux), WebGL2 (Web)
- [ ] Basic camera: WASD + mouse look, free-fly mode
- [ ] Math library integration (HandmadeMath or similar single-header)

### 1.2 64-Bit / Camera-Relative Pipeline
- [ ] Double-precision CPU positions (`double pos_d[3]`) for camera and world objects
- [ ] Camera-relative vertex shader: high/low float split subtraction
- [ ] Logarithmic depth buffer (z_bias per backend: -1 GL, 0 D3D11/Metal/WebGPU)
- [ ] Floating origin system: recenter world origin when camera drifts, regenerate meshes
- [ ] Verify: no jitter or z-fighting at 100km+ from origin

### 1.3 Planet LOD
- [ ] Icosahedron base mesh (20 root triangles, 12 vertices)
- [ ] Aperture-4 recursive subdivision (each tri → 4 children)
- [ ] LOD tree: node pool, split/merge based on `distance < arc × split_factor`
- [ ] Node lifecycle: UNLOADED → GENERATING → READY → ACTIVE
- [ ] Budget caps: max splits/frame, max GPU uploads/frame
- [ ] Threaded mesh generation via job system (lock-free submit from main thread)
- [ ] Body retargeting: reinit LOD tree for any sphere (planet or moon)
- [ ] Geomorphing: smooth vertex interpolation during split/merge transitions

### 1.4 Procedural Terrain
- [ ] Noise stack: continental (FBM), mountain (ridged), warp, detail — all OpenSimplex2
- [ ] Domain warping for organic coastlines
- [ ] Power redistribution: `pow(h, 1.35)` land, `pow(-h, 0.8)` ocean
- [ ] Height → meters conversion (sea level, amplitude, min/max)
- [ ] Biome coloring: height-based bands (sand → grass → rock → snow)
- [ ] Slope computation from noise gradients → material selection

### 1.5 Procedural Terrain Textures
- [ ] Tri-planar mapping to eliminate slope stretching
- [ ] Detail texture layers: macro (biome color), meso (tiling materials), micro (normal maps)
- [ ] 4-channel splat map per patch (rock/grass/sand/snow)
- [ ] Noise-perturbed blend boundaries

### 1.6 Atmosphere
- [ ] Fullscreen ray-march shader (ray-sphere intersection, 8 view samples)
- [ ] Rayleigh + Mie scattering with exponential density falloff
- [ ] Optical depth integration (view ray + sun ray)
- [ ] Sunset warming, tone mapping, dithering
- [ ] Aerial perspective fog in terrain shader (Rayleigh extinction + inscatter)
- [ ] Config-driven parameters (hot-reloadable)

### 1.7 Water
- [ ] Water surface mesh at sea level
- [ ] Gerstner wave displacement (4–8 octaves, vertex shader)
- [ ] Fresnel reflection/refraction blend
- [ ] Sun specular (Blinn-Phong, high shininess)
- [ ] Depth-based color (shallow turquoise → deep blue)
- [ ] Foam at wave peaks via noise threshold

### 1.8 Sky & Lighting
- [ ] Sky dome with day/night color gradient
- [ ] Sun direction driving directional light
- [ ] Terrain lighting: Lambert diffuse, smooth terminator, rim light
- [ ] Ambient occlusion from surface normal vs radial direction

### Phase 1 Milestone
> *Player spawns above a planet. Camera flies freely. Planet has continents, oceans,
> mountains, atmosphere, water with waves, and smooth LOD transitions from orbit to
> surface. No jitter at any scale.*

---

## Phase 2 — Ship Assembly & Launch

**Goal**: Build a rocket from parts, place it on a launch pad, and fly it off the surface.

### 2.1 Part System
- [ ] Part definition format: mass, nodes (stack/radial/docking), mesh, stats
- [ ] Part catalog: command pods, fuel tanks, engines, decouplers, fairings, RCS
- [ ] Engine stats: thrust, Isp (vacuum + atmospheric), gimbal range
- [ ] Fuel tank stats: dry mass, fuel capacity, fuel type

### 2.2 Assembly Building (VAB)
- [ ] Camera: orbit around vehicle, zoom, pan
- [ ] Part placement: pick from catalog, snap to compatible nodes
- [ ] Rotation: 15° increments around attachment axis
- [ ] Symmetry modes: radial (2x/3x/4x/6x), mirror (bilateral)
- [ ] Staging editor: assign parts to numbered stages, drag to reorder
- [ ] Real-time stats: total mass, dry mass, delta-v per stage, TWR, burn time
- [ ] Delta-v via Tsiolkovsky: `Δv = Isp × g₀ × ln(m_wet / m_dry)`
- [ ] Save/load vehicle designs

### 2.3 Launch Complex
- [ ] Procedural site selection: flat, near-equator terrain region
- [ ] Launch pad with flame trench
- [ ] Service tower scaled to vehicle height
- [ ] Assembly building, fuel depot, control bunker (modular placement)
- [ ] Terrain-adaptive foundations (stilts on slopes)

### 2.4 Launch & Ascent
- [ ] Place vehicle on pad, fuel transfer from depot
- [ ] Pre-launch: staging review, abort triggers
- [ ] Throttle control (ramp up/down, full, cut)
- [ ] Stage activation: engine ignition, decoupler separation, fairing jettison
- [ ] Atmospheric drag model: `F = 0.5 × ρ × v² × Cd × A`
- [ ] Max-Q tracking, terminal velocity awareness

### 2.5 Flight Control
- [ ] Pitch/yaw/roll via keyboard (WASDQE)
- [ ] Reaction wheels (always available, low torque)
- [ ] Engine gimbal (pitch/yaw authority during burns)
- [ ] SAS stability hold (lock current heading)
- [ ] Navball: attitude indicator with prograde/retrograde markers

### 2.6 Gravity Turn Autopilot
- [ ] State machine: vertical climb → pitch-over → prograde follow → coast → circularize
- [ ] Altitude/velocity triggers per phase
- [ ] Target apoapsis input, automatic throttle management

### Phase 2 Milestone
> *Player builds a multi-stage rocket in the VAB, launches from a procedural pad,
> flies through atmosphere with drag, stages separation, and reaches orbit using
> manual control or autopilot gravity turn.*

---

## Phase 3 — Orbital Mechanics

**Goal**: Navigate orbit with maneuver nodes, rendezvous and dock ships, assemble
interplanetary vehicles.

### 3.1 Orbit Computation
- [ ] State vector → Keplerian elements conversion (and reverse)
- [ ] Orbit display: Ap, Pe, inclination, eccentricity, period, time-to-Ap/Pe
- [ ] Trajectory prediction: forward RK4 propagation rendered as orbital line
- [ ] Color coding: green (current), orange (post-SOI), blue (post-maneuver), dotted (future)
- [ ] Incremental computation across frames (avoid stalls on long predictions)

### 3.2 Reference Frames
- [ ] Surface frame (launch site origin)
- [ ] Body-centered inertial frame (planet/moon CoM)
- [ ] Target-relative frame (for rendezvous)
- [ ] Barycentric frame (system CoM, for transfers)
- [ ] Player toggle between frames at any time
- [ ] SOI capture: auto-transition when crossing `r_SOI = a × (m/M)^(2/5)`
- [ ] Manual frame lock to prevent auto-switching

### 3.3 Maneuver Nodes
- [ ] Click on trajectory to place node at that true anomaly
- [ ] Three drag handles: prograde, normal, radial (local orbital frame)
- [ ] Real-time trajectory update as handles are dragged
- [ ] Delta-v readout + estimated burn time per node
- [ ] Chain multiple nodes: each downstream node recomputes from prior burns
- [ ] Add/move/adjust/delete nodes at any point during flight
- [ ] Node editing recomputes all downstream trajectories

### 3.4 Burn Execution
- [ ] Autopilot: orient to node direction, throttle at T-0, cutoff at Δv < 0.1 m/s
- [ ] T-minus countdown (start burn at half-burn-time before node)
- [ ] Manual mode: heading indicator + remaining Δv readout as guide
- [ ] RCS for small corrections (< 1 m/s Δv threshold)

### 3.5 SAS Modes
- [ ] Prograde / retrograde / normal± / radial± / target / anti-target / maneuver hold
- [ ] Smooth rotation to target heading via reaction wheels + RCS

### 3.6 Orbit Matching
- [ ] Auto-generate Hohmann transfer (2 burns to match semi-major axis)
- [ ] Plane change burn at ascending/descending node
- [ ] Phase adjustment (period change to close gap)
- [ ] Fine approach corrections (reduce relative velocity to ~0)
- [ ] Generated nodes are editable before execution

### 3.7 Rendezvous & Docking
- [ ] 4-phase approach: approach (10km→1km) → proximity (1km→100m) → final (100m→10m) → dock (10m→contact)
- [ ] Auto frame switching per phase
- [ ] RCS translation mode (HJKL/IN) for docking
- [ ] Docking ports: magnetic capture (1–2m range), alignment cone (15°), hard lock
- [ ] Port sizes: small (0.5m), medium (1.0m), large (2.0m), grabber (any surface)
- [ ] Relative nav HUD: closing speed, distance, port alignment indicator

### 3.8 Vehicle Assembly in Orbit
- [ ] Docked vessels merge into single rigid body
- [ ] Combined mass/delta-v/TWR recalculation
- [ ] Staging reconfiguration post-dock
- [ ] Undocking: spring separation, independent vessel creation
- [ ] Focus switching between vessels

### 3.9 Time Warp
- [ ] Warp rates: 2x, 5x, 10x, 50x, 100x, 1000x (altitude-gated)
- [ ] Auto-warp-to-node: stop at T-30s before next maneuver
- [ ] Warp cancel: collision warning, SOI transition, player input
- [ ] No warp during burns or proximity ops (< 500m)
- [ ] Maneuver node editing during warp

### Phase 3 Milestone
> *Player plans and executes orbital maneuvers with nodes. Can rendezvous with a
> second vessel, dock, assemble a larger ship, plan a transfer to a moon, and
> execute the transfer burn. Full reference frame switching and time warp.*

---

## Phase 4 — Solar System & N-Body Physics

**Goal**: A full solar system with moons, GPU-accelerated n-body gravity, and
interplanetary travel.

### 4.1 Solar System Setup
- [ ] Central planet + multiple moons with distinct properties
- [ ] Per-body: radius, mass, surface gravity, orbit parameters, visual style
- [ ] Ellipsoid/capsule shape variants for moons
- [ ] Procedural terrain per body (noise seeds, amplitude, biome palette)
- [ ] Moon-specific atmosphere (optional, thinner)

### 4.2 N-Body Gravity (CPU Path)
- [ ] Replace Kepler solver with RK4 n-body integration for all bodies
- [ ] Fixed timestep accumulator (deterministic, frame-rate independent)
- [ ] Direct summation O(n²) with softening parameter
- [ ] Job system parallelization: per-thread force buffers, deterministic reduction
- [ ] Kepler fast-path retained for unperturbed 2-body (distant/stable orbits)
- [ ] Energy monitoring: track total orbital energy drift

### 4.3 N-Body Gravity (GPU Compute)
- [ ] Sokol compute pipeline: storage buffers, compute dispatch
- [ ] Tile-based kernel: load body tile into shared memory, accumulate forces
- [ ] Workgroup sizing: 128–256 threads (tune per backend)
- [ ] Double-buffered position/velocity SSBOs (ping-pong)
- [ ] Backend support: D3D11 UAVs, Metal threadgroups, WebGPU workgroup storage
- [ ] CPU↔GPU threshold: < 50 bodies → CPU, ≥ 50 → GPU (profile and tune)
- [ ] Readback positions to `CelestialBody.pos_d[]` for renderer

### 4.4 Determinism
- [ ] Consistent force accumulation order (thread buffer index 0→N)
- [ ] Kahan compensated summation for long simulations
- [ ] Compiler flags: no `-ffast-math`, `-ffp-contract=off` where needed
- [ ] Verify: save/reload produces identical trajectories

### 4.5 SOI & Multi-Body Navigation
- [ ] SOI radius computation per body: `r_SOI = a × (m/M)^(2/5)`
- [ ] Patched conics: trajectory prediction across SOI boundaries
- [ ] Trajectory color changes at SOI transitions
- [ ] Transfer window finder: optimal phase angles for Hohmann transfers
- [ ] Delta-v map: node graph of all bodies with transfer costs on edges
- [ ] Porkchop plot: departure vs arrival date, colored by total Δv

### 4.6 Mission Planning
- [ ] Multi-SOI maneuver node chaining
- [ ] Full mission overview: launch → orbit → transfer → capture → land
- [ ] Total Δv budget vs vehicle capability warning
- [ ] Closest approach finder for selected targets

### 4.7 Interplanetary Travel
- [ ] Long-duration coast with high time warp (1000x in deep space)
- [ ] Mid-course corrections via maneuver nodes
- [ ] Arrival: capture burn into moon orbit
- [ ] Landing: deorbit + powered descent (reverse gravity turn)
- [ ] Surface operations on destination body (launch complex generation)

### Phase 4 Milestone
> *Full solar system with a planet and multiple moons. Bodies interact via n-body
> gravity (GPU-accelerated). Player plans interplanetary missions with transfer
> windows and porkchop plots, travels between bodies, lands on moons, and builds
> new launch infrastructure. Physics is deterministic and stable over long time scales.*

---

## Cross-Cutting Concerns

These systems evolve across all phases:

| System | Phase 1 | Phase 2 | Phase 3 | Phase 4 |
|--------|---------|---------|---------|---------|
| Camera | Free-fly | Vehicle-follow + free | Orbit map view | System map view |
| UI/HUD | Debug stats | Navball, throttle | Orbit info, node editor | Delta-v map, mission planner |
| Audio | Ambient wind | Engine roar, staging | Silence of space | Per-body ambient |
| Persistence | — | Vehicle designs | Vessel orbits | Full solar system state |
| Multiplayer | — | — | — | Deterministic lockstep foundation |
