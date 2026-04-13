# Gameplay Systems

Design reference for Orbital-Frontier's core gameplay loop: build rockets, launch from
procedural complexes, perform orbital maneuvers, dock to assemble interplanetary vehicles,
and travel between celestial bodies.

---

## 1. Procedural Launch Complex

### Overview

Each planet/moon with sufficient gravity generates a launch complex at a fixed surface
location. The complex is procedurally assembled from modular pieces based on the body's
properties — gravity, atmosphere density, and available resources determine scale and layout.

### Complex Components

| Component        | Function                                    | Generation Rule                  |
|------------------|---------------------------------------------|----------------------------------|
| Launch pad       | Rocket placement + flame trench             | Always present, sized to body gravity |
| Service tower    | Fuel lines, crew access, umbilicals         | Height scales with max rocket size |
| Fuel depot       | Propellant storage + transfer               | Capacity ∝ body resource richness |
| Assembly building| Rocket construction + storage               | Footprint ∝ tech level           |
| Control bunker   | Launch sequencing, abort triggers           | Always present                   |
| Landing zone     | Flat pad for returning stages               | Present if body has atmosphere   |

### Procedural Layout

1. **Site selection**: find a flat terrain region (low slope) near the equator
   for maximum rotational velocity bonus
2. **Pad placement**: central anchor point, flame trench oriented away from structures
3. **Support buildings**: radial placement around pad with minimum blast radius clearance
4. **Road/rail network**: pathfinding between buildings across terrain mesh
5. **Terrain adaptation**: structures snap to terrain surface heights, stilts/foundations
   generated to handle uneven ground

### Body-Specific Variation

- **High gravity** (planet): large reinforced pad, tall service tower, heavy fuel infrastructure
- **Low gravity** (small moons): minimal pad, no tower needed, lightweight structures
- **No atmosphere**: no aerodynamic fairings needed, simpler launch profiles
- **Extreme terrain**: elevated platform on stilts, longer access roads

---

## 2. Rocket Building System

### Design Philosophy

KSP-inspired snap-together construction. Parts are categorized by function. The player
assembles rockets in the assembly building from a parts catalog, with real-time
mass/delta-v/TWR feedback.

### Part Categories

| Category     | Examples                                      | Key Stats                    |
|--------------|-----------------------------------------------|------------------------------|
| Command      | Capsule, probe core, docking adapter          | Crew capacity, control authority |
| Fuel tanks   | Small/med/large cylindrical, radial, drop     | Dry mass, fuel mass, fuel type |
| Engines      | Liquid, solid, ion, RCS                       | Thrust, Isp (vac/atm), gimbal |
| Structural   | Decouplers, struts, fairings, interstages     | Mass, attachment nodes       |
| Utility      | Solar panels, batteries, antennas, lights     | Power gen/storage, data rate |
| Aerodynamic  | Nose cones, fins, control surfaces            | Drag coefficient, lift       |
| Docking      | Docking port (sm/md/lg), grabber claw         | Port size, magnetic strength |

### Attachment System

Parts expose **nodes** — typed connection points:

- **Stack nodes**: top/bottom of cylindrical parts (axial attachment)
- **Radial nodes**: surface-mount points (lateral attachment)
- **Docking nodes**: same as stack but support in-flight connect/disconnect

Snapping: when a held part's node approaches a compatible open node within threshold
distance, highlight and snap. Rotation in 15-degree increments around the attachment axis.

### Staging

Parts are assigned to numbered stages (bottom = first to fire). Stage activation fires
all parts in that stage simultaneously:

- Engines: ignite
- Decouplers: separate
- Fairings: jettison
- Parachutes: deploy

The staging sequence is editable in the assembly building and in-flight.

### Vehicle Stats (Real-Time Feedback)

Displayed during construction and updated live:

```
Total mass:       12,400 kg
Dry mass:          4,200 kg
Delta-v (vacuum): 3,840 m/s
Delta-v (atm):    2,910 m/s
TWR (surface):    1.42
TWR (vacuum):     1.87
Burn time:        124s (stage 1) → 89s (stage 2)
```

**Delta-v per stage** via the Tsiolkovsky rocket equation:

```
Δv = Isp × g₀ × ln(m_wet / m_dry)
```

Where `g₀ = 9.81 m/s²`, `m_wet` = stage mass with fuel, `m_dry` = stage mass without fuel.

### Symmetry Modes

- **Radial symmetry**: 2x, 3x, 4x, 6x — place one part, copies appear at equal angles
- **Mirror symmetry**: bilateral (left/right) for aerodynamic builds
- Toggle via hotkey during construction

---

## 3. Launch & Ascent

### Launch Sequence

1. Player places rocket on pad (or loads saved design)
2. Pre-launch checklist: fuel levels, staging order, abort triggers
3. Throttle up → release clamps → liftoff
4. Player controls pitch/yaw/roll or engages autopilot gravity turn

### Gravity Turn

Optimal ascent trades vertical climb for horizontal velocity:

- **0–100m**: vertical climb to clear tower
- **100m–10km**: gradual pitch-over toward horizon (rate depends on TWR and atmosphere)
- **10km–apoapsis**: follow prograde, reduce throttle as apoapsis approaches target altitude
- **Circularization**: coast to apoapsis, burn prograde to raise periapsis

Autopilot implements this as a state machine with altitude/velocity triggers.

### Atmospheric Effects

On bodies with atmosphere:
- **Drag**: `F_drag = 0.5 × ρ × v² × Cd × A` — penalizes high speed at low altitude
- **Max-Q**: dynamic pressure peak — structural stress limit
- **Terminal velocity**: equilibrium between thrust+gravity and drag
- Fairings protect payload; jettisoned above ~70% atmosphere height

---

## 4. Orbital Mechanics Gameplay

### Reference Frame System

The player can switch reference frames at any time. The active frame determines how
trajectories, velocities, and maneuver nodes are displayed and computed.

| Frame             | Origin          | Use Case                          |
|-------------------|-----------------|-----------------------------------|
| Surface           | Launch site     | Launch, landing, ground ops       |
| Body-centered     | Planet/moon CoM | Orbit visualization, planning     |
| Target-relative   | Target vessel   | Rendezvous, docking approach      |
| Barycentric       | System CoM      | Transfer orbits, multi-body planning |

**Capture system**: when a vessel crosses a body's sphere of influence (SOI), the
reference frame automatically transitions. The trajectory is re-computed in the new
body's frame. Player can also manually lock a reference frame to prevent auto-switching.

SOI radius for each body: `r_SOI = a × (m_body / m_parent)^(2/5)` where `a` is the
semi-major axis of the body's orbit.

### Trajectory Prediction

A forward simulation (using the same RK4 integrator from `physics_engine.md`) propagates
the vessel's state vector into the future. The predicted trajectory is rendered as a
colored orbital line:

- **Green**: current orbit in active SOI
- **Orange**: predicted orbit after SOI transition
- **Blue**: planned trajectory (post-maneuver)
- **Dotted**: trajectory segments beyond next maneuver node

Prediction horizon: configurable (1 orbit, 5 orbits, or time-based). Longer predictions
are computed incrementally across frames to avoid stalls.

### Orbit Display

Standard Keplerian elements displayed in the HUD:

```
Apoapsis:     250.3 km        Time to Ap:  12m 34s
Periapsis:    185.7 km        Time to Pe:   3m 12s
Inclination:   28.5°          Period:      91m 20s
Eccentricity:   0.017         Semi-major:  217.8 km
```

---

## 5. Maneuver Nodes

### Overview

Maneuver nodes are placed on the orbital trajectory to plan burns. Each node defines a
velocity change (delta-v) vector in the local orbital frame at a specific point along
the trajectory. The flight computer can then execute the burn automatically.

### Node Placement

- Click anywhere on the predicted trajectory line to place a node
- Node position = true anomaly (angle along orbit) at placement point
- Drag node along trajectory to change burn timing
- Multiple nodes can be chained — each subsequent node's trajectory accounts for all
  prior planned burns

### Delta-V Handles

Each node has three orthogonal drag handles in the local orbital frame:

| Handle    | Direction         | Effect                                    |
|-----------|-------------------|-------------------------------------------|
| Prograde  | Along velocity    | Raise/lower opposite side of orbit        |
| Normal    | Perpendicular to orbital plane | Change inclination          |
| Radial    | Toward/away from body center   | Rotate orbit orientation    |

Dragging a handle adjusts the delta-v magnitude in that axis. Total delta-v and
estimated burn time update in real-time. The predicted trajectory (blue line) updates
as the player adjusts.

### Node Editing at Any Point

Nodes are fully editable at any time during flight:
- **Add**: click trajectory to insert new node
- **Move**: drag node along trajectory to retiming
- **Adjust**: drag handles to change delta-v vector
- **Delete**: right-click or delete key
- **Reorder**: nodes execute in chronological order along trajectory

Editing a node recomputes all downstream trajectories (nodes after it update
their predicted paths).

### Burn Execution

When a maneuver node is active (next chronologically):

1. **T-minus display**: countdown to burn start (half burn time before node point)
2. **Auto-orient**: autopilot rotates vessel to burn direction (prograde/normal/radial mix)
3. **Burn**: throttle up at T-0, main engine or RCS depending on delta-v magnitude
4. **Cutoff**: throttle down when remaining delta-v < threshold (0.1 m/s default)

The player can choose:
- **Autopilot execution**: the flight computer handles orientation and throttle precisely
- **Manual execution**: player orients and throttles by hand, node provides a
  heading indicator and remaining-delta-v readout as guide

---

## 6. Manual Flight Control

### Orientation

| Input           | Action                              |
|-----------------|-------------------------------------|
| W/S             | Pitch up/down                       |
| A/D             | Yaw left/right                      |
| Q/E             | Roll left/right                     |
| Mouse drag      | Free-look (camera only)             |

Rotation is applied via reaction wheels (always available, low torque) and RCS thrusters
(if equipped, high torque). Engine gimbal provides additional pitch/yaw authority
during burns.

### Throttle

| Input           | Action                              |
|-----------------|-------------------------------------|
| Shift           | Throttle up (hold = ramp)           |
| Ctrl            | Throttle down (hold = ramp)         |
| Z               | Full throttle                       |
| X               | Cut throttle                        |
| Space           | Next stage                          |

### Translation (RCS Docking Mode)

When RCS is enabled and docking mode is active:

| Input           | Action                              |
|-----------------|-------------------------------------|
| H/N             | Translate forward/back              |
| I/K             | Translate up/down                   |
| J/L             | Translate left/right                |

### SAS Hold Modes

Stability Assist System locks orientation to a reference:

| Mode        | Locks To              | Use Case                        |
|-------------|-----------------------|---------------------------------|
| Stability   | Current heading       | Hold attitude                   |
| Prograde    | Velocity vector       | Efficient burns                 |
| Retrograde  | Anti-velocity         | Deorbit, braking                |
| Normal+/-   | Orbit normal          | Inclination changes             |
| Radial+/-   | Radial direction      | Orbit rotation                  |
| Target       | Toward target         | Rendezvous approach             |
| Anti-target  | Away from target      | Departure                       |
| Maneuver    | Next node direction   | Burn alignment                  |

---

## 7. Orbit Matching & Rendezvous

### Automatic Orbit Matching

The player selects a target vessel or body and requests "match orbit." The flight
computer generates a multi-node maneuver sequence:

1. **Hohmann transfer**: two-burn sequence to match target's semi-major axis
2. **Plane change**: normal burn at ascending/descending node to match inclination
3. **Phase adjustment**: if target is in same orbit, adjust period to close the gap
4. **Fine approach**: low-thrust corrections to reduce relative velocity to near-zero

The generated nodes appear on the trajectory and can be edited before execution.

### Rendezvous Sequence

Once orbits are matched (within tolerance):

```
Phase 1 — Approach    (10km → 1km)   Body-centered frame
          Close at 20 m/s, brake at 1km

Phase 2 — Proximity   (1km → 100m)   Target-relative frame
          Close at 5 m/s, brake at 100m

Phase 3 — Final       (100m → 10m)   Target-relative frame
          Close at 1 m/s, brake at 10m

Phase 4 — Docking     (10m → contact) Target-relative frame
          Translate at 0.2 m/s, align docking ports
```

Each phase automatically switches to the appropriate reference frame. The player can
take manual control at any phase.

### Relative Navigation Display

In target-relative frame, the HUD shows:
- Relative velocity vector (prograde/retrograde markers)
- Distance to target
- Closing speed
- Docking port alignment indicator (when within 50m)
- Time to closest approach

---

## 8. Docking & Vehicle Assembly

### Docking Mechanics

Docking ports have:
- **Capture range**: magnetic pull activates within 1–2m (size-dependent)
- **Alignment cone**: ports must be within ~15° of axial alignment to capture
- **Magnetic guidance**: once in capture range, ports auto-align and pull together
- **Hard dock**: once contact is made and aligned, ports lock — vessels become one rigid body

### Port Compatibility

| Port Size | Diameter | Compatible With     |
|-----------|----------|---------------------|
| Small     | 0.5m     | Small only          |
| Medium    | 1.0m     | Medium only         |
| Large     | 2.0m     | Large only          |
| Grabber   | N/A      | Any surface (no port needed) |

### In-Orbit Assembly

The core gameplay loop for interplanetary travel:

1. **Launch payload** — fuel tanks, engines, habitation modules sent to orbit individually
2. **Rendezvous** — each payload matches orbit with the growing vessel
3. **Dock** — connect modules via docking ports to build the interplanetary ship
4. **Configure** — set staging for the assembled vehicle, balance fuel, test systems
5. **Depart** — plan transfer burn with maneuver nodes, execute, coast to destination

Assembled vehicles inherit all parts from docked sub-vessels. The combined mass, delta-v,
and TWR update immediately. Staging can be reconfigured post-dock.

### Undocking

Any docking port can be undocked at any time:
- Undocked vessel becomes independent (own orbit, own staging)
- Gentle separation push (spring force) prevents immediate collision
- Player switches focus to either vessel

---

## 9. Time Management

### Real-Time with Burn Windows

The game runs in real-time. Between burns, the player may need to wait for the correct
orbital position. During these coasting phases:

- **Time warp**: accelerate time (2x, 5x, 10x, 50x, 100x) during coast phases
- **Auto-warp to node**: warp automatically stops at T-minus 30s before next maneuver
- **No warp during burns**: time warp disabled while engines are firing or during
  proximity operations (< 500m from another vessel)

### Warp Constraints

| Altitude           | Max Warp |
|--------------------|----------|
| Below 10km         | 2x       |
| 10–50km            | 10x      |
| 50–200km           | 50x      |
| Above 200km        | 100x     |
| Deep space          | 1000x    |

Warp is instantly cancelled if: collision warning, SOI transition, maneuver node
approach, or player input.

### Trajectory Planning During Warp

Maneuver nodes can be placed and edited while time-warped. The trajectory prediction
runs independently of the warp rate. This lets players plan complex multi-burn
sequences during coast phases without waiting in real-time.

---

## 10. Delta-V Budget & Mission Planning

### Delta-V Map

A reference showing approximate delta-v costs between locations:

```
Surface → Low Orbit:    ~3,500 m/s  (varies by body)
Low Orbit → Transfer:   ~900 m/s    (Hohmann to nearest moon)
Transfer → Capture:     ~600 m/s    (braking into moon orbit)
Moon Orbit → Landing:   ~500 m/s    (depends on moon gravity)
```

Displayed in-game as a node graph connecting all reachable bodies with delta-v costs
on each edge. Updates based on current orbital positions (transfer windows).

### Transfer Windows

Hohmann transfers are most efficient at specific phase angles. The game provides:
- **Transfer window indicator**: highlights optimal departure timing on current orbit
- **Porkchop plot** (simplified): 2D grid of departure date vs arrival date,
  colored by total delta-v cost — lets players find the cheapest transfer
- **Closest approach finder**: for a selected target, find the next N closest
  approach opportunities and their delta-v costs

### Mission Planner

An overlay that lets the player chain together maneuver nodes across multiple SOIs
to plan an entire mission:

```
Launch → Orbit (3400 Δv) → Transfer (850 Δv) → Capture at Gorrath (620 Δv)
  → Lower orbit (200 Δv) → Land (480 Δv)
  Total: 5,550 m/s  |  Margin: 1,200 m/s remaining
```

Warns if the vehicle's total delta-v is insufficient for the planned mission.

---

## References

| Topic | Source |
|-------|--------|
| Rocket equation | [Wikipedia: Tsiolkovsky rocket equation](https://en.wikipedia.org/wiki/Tsiolkovsky_rocket_equation) |
| Hohmann transfer | [Wikipedia: Hohmann transfer orbit](https://en.wikipedia.org/wiki/Hohmann_transfer_orbit) |
| Gravity turn | [Wikipedia: Gravity turn](https://en.wikipedia.org/wiki/Gravity_turn) |
| Sphere of influence | [Wikipedia: Sphere of influence (astrodynamics)](https://en.wikipedia.org/wiki/Sphere_of_influence_(astrodynamics)) |
| Orbital mechanics | [Braeunig, "Orbital Mechanics"](http://www.braeunig.us/space/orbmech.htm) |
| Porkchop plots | [NASA JPL, "Trajectory Browser"](https://trajbrowser.arc.nasa.gov/) |
| KSP delta-v map | [KSP Delta-v Planner](https://meyerweb.com/eric/ksp/delta-v/) |
| Rendezvous | [Wikipedia: Space rendezvous](https://en.wikipedia.org/wiki/Space_rendezvous) |
| Docking mechanics | [NASA, "ISS Docking System"](https://www.nasa.gov/feature/docking-system) |
| Maneuver nodes | [KSP Wiki: Maneuver node](https://wiki.kerbalspaceprogram.com/wiki/Maneuver_node) |
| Gerstner waves | [GPU Gems 1 Ch. 1, "Effective Water Simulation"](https://developer.nvidia.com/gpugems/gpugems/part-i-natural-effects/chapter-1-effective-water-simulation-physical-models) |
| Atmospheric drag | [Wikipedia: Atmospheric entry](https://en.wikipedia.org/wiki/Atmospheric_entry) |
