# Planetary Rendering Pipeline

Technical reference for the Orbital-Frontier planetary rendering system built on C + Sokol.
Source engine: [Caelum](https://github.com/RubenTipparach/Caelum) (`hex-planets/`).

---

## 1. Sokol + C Rendering Stack

### Overview

Sokol is a set of single-header C99 libraries providing a cross-platform graphics abstraction.
The engine uses a Pipeline State Object (PSO) model — immutable pipelines created at init,
bound per draw call.

### Backend Matrix

| Platform    | Backend   | Define          | Build                |
|-------------|-----------|-----------------|----------------------|
| Windows     | D3D11     | `SOKOL_D3D11`   | CMake (MSVC/MinGW)   |
| macOS       | Metal     | `SOKOL_METAL`   | CMake (Xcode)        |
| Linux       | OpenGL    | `SOKOL_GLCORE`  | CMake (GCC/Clang)    |
| Web         | WebGL2    | `SOKOL_GLES3`   | Emscripten           |
| Web (future)| WebGPU    | `SOKOL_WGPU`    | Emscripten           |

Backend selection: `Caelum/CMakeLists.txt` lines 14–22.

### Shader Cross-Compilation

Shaders are authored as annotated `.glsl` files using `@vs`, `@fs`, `@program` markers.
The `sokol-shdc` tool (`Caelum/tools/sokol-shdc.exe`) compiles them into `.glsl.h` headers
containing backend-specific bytecode (HLSL5, GLSL430, GLSL300ES, MSL, WGSL).

Shader pairs in `Caelum/shaders/`:
`planet`, `atmosphere`, `sky`, `highlight`, `torch`, `agent`, `hotbar`.

### Pipeline Architecture

The `Renderer` struct (`Caelum/src/render.h`) holds separate `sg_pipeline` objects for each
pass. Typical draw pattern:

```c
sg_apply_pipeline(pip);
sg_apply_bindings(&bind);       // vertex/index buffers + textures
sg_apply_uniforms(slot, &ub);   // per-draw uniform block
sg_draw(base, count, instances);
```

Render order: sky dome → planet LOD → terrain → water → atmosphere (additive) → torch lights → agents → UI.

---

## 2. Procedural Planet Generation

### Noise Stack

Four layers defined in `Caelum/src/terrain_noise.h`, all using FastNoiseLite (OpenSimplex2):

| Layer       | Type   | Freq | Octaves | Seed Offset | Role                        |
|-------------|--------|------|---------|-----------|-----------------------------|
| Continental | FBM    | 0.6  | 3       | +0        | Landmass / ocean shapes     |
| Mountain    | Ridged | 1.5  | 5       | +4000     | Sharp peaks and ridgelines  |
| Warp        | FBM    | 4.0  | 3       | +1000     | Domain warping (coastlines) |
| Detail      | Ridged | 16.0 | 3       | +2000     | Fine surface variation      |

### Composition Pipeline

```
warp_offset  = warp_noise(lon, lat) * 0.5
sample_point = (lon, lat) + warp_offset

continent = continental_noise(sample_point)
mountain  = ridged_noise(sample_point)
mountain  = remap(mountain, [-1,1] -> [0,1])²
land_mask = smoothstep(-0.05, 0.35, continent)

height = continent * 0.55 + mountain * land_mask * 0.45 + detail * land_weight

// Power redistribution
if height > 0: height = pow(height, 1.35)   // steepen mountains
if height < 0: height = -pow(-height, 0.8)  // flatten ocean floors
```

### Height Conversion

`ht_sample_height_m()` maps normalized `[-1, 1]` noise to meters:
- Sea level: 4000m
- Amplitude: 8000m
- Range: 500m (deep ocean) to 8500m (highest peaks)

### Hydraulic Erosion

Particle-based droplet simulation in `Caelum/src/erosion.h`:
- 5000 droplets per region, max 80 steps per droplet
- 256×256 sample grid per region (768m × 768m), 96m overlap borders
- LRU cache: 32 regions, disk-backed binary persistence
- Brush radius 3, inertia 0.3, erosion rate 0.7, deposition 0.02
- Job-system parallelized: one compute job per frame

### Moon Variants

10 moons in `Caelum/src/celestial.h` with shape diversity:
- **Ellipsoid** mode: standard surface normal `normalize(dir/scale²)`
- **Capsule** mode: for elongated bodies (axis_ratio > 1.5) — cylinder + hemispherical endcaps
- Noise displacement: 1% of base radius (10–800m features)
- Crater generation: power-law size distribution, parabolic bowl + Gaussian rim

### Biome System

Current: height-only bands with smooth blending:
- Sand (0–200m) → Grass (200–1500m) → Stone (1500–4500m) → Ice (4500m+)
- Transitions use `smoothstepf` + deterministic dithering (±100m zones)

Future (Phase 4): Whittaker-style latitude/altitude → temperature, ocean proximity → moisture, 12-biome grid.

---

## 3. Atmosphere Rendering

### Current Implementation

Fullscreen ray-march pass in `Caelum/shaders/atmosphere.glsl`:
- Ray-sphere intersection against planet radius and atmosphere radius
- 8 sample points along view ray, 4 light-ray samples per point for optical depth
- Additive blending over the scene

### Scattering Model

**Rayleigh** (air molecules): wavelength-dependent `(1/λ)⁴`
- Coefficients: `vec3(5.602, 9.473, 19.644)` for RGB (680/550/440 nm)
- Exponential density falloff with altitude (scale height ~8.5km)

**Mie** (aerosols): Henyey-Greenstein phase function
- Asymmetry parameter `g ≈ 0.85` (strong forward scattering)
- Density scale height ~1.2km (concentrated near surface)

### Optical Depth Integration

For each view-ray sample point, a secondary ray is cast toward the sun. The accumulated
optical depth along both rays determines extinction and inscattering:

```
transmittance = exp(-(rayleigh_depth * beta_r + mie_depth * beta_m))
inscatter     = phase_rayleigh * beta_r + phase_mie * beta_m
```

### Visual Enhancements

- **Sunset warming**: explicit warm tint near sun, golden glow, horizon band warmth
- **Dithering**: small noise offset to prevent color banding in gradients
- **Tone mapping**: `1 - exp(-color)` (simple Reinhard variant)

### Aerial Perspective

Replicated inline in `Caelum/shaders/planet.glsl` (lines 190–228): Rayleigh extinction +
inscatter blended with terrain fragment color. Vacuum clipping disables fog when camera
is above atmosphere.

Config: `AtmosphereConfig` struct — planet_radius, atmosphere_radius, rayleigh_scale,
mie_scale, mie_g, sun_intensity. Hot-reloadable via `config.yaml` (R key).

### Future: Precomputed LUT

Bruneton & Neyret 2008 — precompute light transport into a 4D lookup table (view zenith,
sun zenith, altitude, view-sun angle). Runtime cost: O(1) per pixel via texture fetches.
Supports multiple scattering orders. Would replace the current ray-march for distant views.

---

## 4. Terrain Rendering & Self-Shadowing

### Current Lighting Model

From `Caelum/shaders/planet.glsl`:

| Component          | Technique                                    |
|--------------------|----------------------------------------------|
| Terminator         | `smoothstep(-0.1, 0.3, NdotL)` for soft edge |
| Ambient occlusion  | `dot(N, surface_dir)` via smoothstep (45–100%)|
| Diffuse            | Lambert, gated by sun_brightness              |
| Ocean specular     | Blinn-Phong (H=32), masked by blue vertex color|
| Rim light          | `pow(1 - NdotV, 3) * 0.12`                   |
| Shadow desat       | Reduce saturation in dark regions             |

### Slope Data

Per-vertex slope computed from noise gradients during mesh generation:
- Steepness magnitude — used for slope-based material blending
- East/north gradient components — drive normal perturbation
- Steep faces → exposed rock texture; gentle slopes → biome ground cover

### LOD Transition Blending

Bayer-dithered discard at LOD transition distances: fragments are probabilistically
discarded based on a 4×4 Bayer matrix + distance ratio. Creates smooth visual fade
between adjacent LOD levels without popping artifacts (geomorphing).

### Procedural Terrain Textures

Terrain color is generated procedurally rather than sampled from a single global texture:

**Tri-planar mapping**: project texture coordinates from world X/Y/Z axes, blend
by surface normal weight. Eliminates stretching on steep slopes that plagues UV-based
approaches. See [GPU Gems 3 Ch. 1](https://developer.nvidia.com/gpugems/gpugems3/part-i-geometry/chapter-1-generating-complex-procedural-terrains-using-gpu).

**Detail texturing layers**:
- **Macro** (satellite view): biome color from noise (continental scale)
- **Meso** (mid-range): tiling rock/grass/sand textures, tri-planar projected
- **Micro** (close-up): high-frequency detail normal maps for surface roughness

**Texture splatting**: blend between material textures using slope, altitude, and
noise-based weights. 4-channel splat map per LOD patch (R=rock, G=grass, B=sand, A=snow).
Transition zones use smooth `mix()` with noise perturbation to avoid straight-line boundaries.

**Virtual texturing** (future): generate and cache terrain texture tiles on demand.
Only resident tiles are GPU-loaded. Clipmap-style ring structure centered on camera.
See [id Tech 5 Megatexture](https://advances.realtimerendering.com/s2008/Mittring-AdvancedVirtualTextureTechniques.pdf).

### Water Rendering

Ocean and lake surfaces require dedicated shading distinct from terrain:

**Geometry**: water surface rendered as a separate mesh at sea level altitude (4000m).
On close LOD patches, displaced by vertex-shader wave animation. On distant patches,
flat plane with normal-map-only waves.

**Wave model**: sum of Gerstner waves at multiple frequencies/directions:

```glsl
// Per-wave displacement (vertex shader)
vec3 gerstner(vec2 pos, float amp, float freq, vec2 dir, float phase, float steep) {
    float theta = dot(dir, pos) * freq + phase;
    return vec3(steep * amp * dir.x * cos(theta),
                amp * sin(theta),
                steep * amp * dir.y * cos(theta));
}
```

4–8 wave octaves provide convincing ocean motion. See [GPU Gems 1 Ch. 1](https://developer.nvidia.com/gpugems/gpugems/part-i-natural-effects/chapter-1-effective-water-simulation-physical-models).

**Fragment shading**:
- **Fresnel reflection/refraction**: `mix(refract_color, reflect_color, pow(1-NdotV, 5))`
- **Specular**: sun reflection via Blinn-Phong with high shininess (H=256)
- **Subsurface scattering**: approximate with view-dependent translucency near wave crests
  (light passing through thin water). See [GPU Pro 2](https://www.taylorfrancis.com/books/9781568817200) ocean rendering chapter.
- **Depth-based color**: shallow water → turquoise tint (scatter), deep water → dark blue (absorption)
- **Foam**: white caps at wave peaks using noise threshold on wave height derivative
- **Caustics** (future): projected animated caustic texture on underwater terrain

**Reflection**:
- Planar reflection (render scene mirrored at water plane) for high quality
- Screen-space reflection (SSR) as cheaper alternative
- Cubemap fallback for distant water

**Underwater**:
- Tinted fog (exponential, blue-green) when camera submerges
- Distortion post-process via animated normal map
- God rays via radial blur toward sun direction

### Future: Self-Shadowing

**Horizon Mapping**: precompute max elevation angle at N directions (typically 8–16) per
terrain element. At render time, compare sun elevation angle against stored horizon — if
below, the point is in shadow. Multi-resolution pyramid allows soft shadows at varying scales.

**Runtime Ray-March**: cast shadow rays along terrain heightfield. More flexible (dynamic
light), more expensive. Could leverage existing slope gradient data for acceleration.

---

## 5. Planet LOD System

### Architecture

Icosahedron-based aperture-4 subdivision defined in `Caelum/src/lod.h`:

| Parameter         | Value  |
|-------------------|--------|
| Root faces        | 20 (icosahedron) |
| Max depth         | 13     |
| Node pool         | 16,384 |
| Split factor      | 8.0 (configurable) |
| Splits/frame cap  | 256    |
| GPU uploads/frame | 64     |
| Worker threads    | 4      |

### Node Lifecycle

```
UNLOADED ──▶ GENERATING ──▶ READY ──▶ ACTIVE
   ▲                                     │
   └─────────── (merge) ─────────────────┘
```

- **GENERATING**: mesh job submitted to thread pool via `Caelum/src/job_system.h`
- **READY**: mesh computed, awaiting GPU upload slot
- **ACTIVE**: vertex buffer on GPU, rendering

### Split/Merge Decision

Split when: `camera_distance < depth_arc[depth] × split_factor`

`depth_arc[]` is precomputed per level — the angular size of a patch at each depth.
Budget caps prevent frame stalls: max 256 splits and 64 GPU uploads per frame.

### Maximum Depth Patches

At maximum depth (13), triangle patches are small enough for close-range terrain detail:
- **Tangent frame**: shared global tangent frame anchored on sphere surface, re-anchored
  when camera drifts far — keeps vertex positions in float32 range
- **Detail mesh**: higher vertex density per patch, procedural texture splatting at full resolution
- **Geomorphing**: vertices interpolate smoothly between parent and child positions during
  split/merge to eliminate popping. Morph factor based on distance within the split threshold band.

### Body Retargeting

`lod_tree_retarget()` destroys all nodes and reinitializes for any `CelestialBody` —
planet or moon. `max_depth_effective` is capped by body radius (small moons don't need
13 levels). Per-level stats track patch count, vertex count, min/max distance.

---

## 6. 64-bit / Camera-Relative Rendering

### Problem

Single-precision floats (23-bit mantissa) lose sub-centimeter precision beyond ~10km from
origin. At planetary scale (800km radius), position quantization reaches ~6cm — visible as
vertex jitter and z-fighting.

### Double-Precision CPU Positions

All world-space positions stored as `double`:
- `Camera.pos_d[3]` — player position
- `CelestialBody.pos_d[3]` — body centers
- `LodTree.world_origin[3]` — floating origin anchor

### Camera-Relative Vertex Shader

Positions are split into high/low float pairs before GPU upload. The vertex shader
reconstructs camera-relative coordinates:

```glsl
vec3 cam_rel = (a_position - camera_offset.xyz) - camera_offset_low.xyz;
```

This preserves sub-millimeter precision regardless of distance from world origin.
See `Caelum/shaders/planet.glsl` lines 28–29.

### Logarithmic Depth Buffer

Standard depth buffers waste precision on near geometry. Log depth distributes precision
evenly across the full range:

```glsl
gl_Position.z = (log2(max(1e-6, 1.0 + w)) * Fcoef + z_bias) * w;
```

- `z_bias = -1` for GL (NDC `[-1,1]`), `0` for D3D11/Metal/WebGPU (NDC `[0,1]`)

### Floating Origin Recentering

`lod_tree_update_origin()` monitors camera drift from `world_origin`. When drift exceeds
a threshold, the origin recenters to the camera and all LOD meshes are regenerated with
updated vertex positions. This keeps float32 vertex coords near zero at all times.

`lod_tree_terrain_height()` returns height as `double` to avoid quantization in the
ground-contact calculation.

---

## References

| Topic | Source |
|-------|--------|
| Sokol | [github.com/floooh/sokol](https://github.com/floooh/sokol) |
| Sokol compute | [Sokol compute update (Mar 2025)](https://floooh.github.io/2025/03/03/sokol-gfx-compute-update.html) |
| Atmosphere | [Bruneton & Neyret 2008, "Precomputed Atmospheric Scattering"](https://inria.hal.science/inria-00288758/document) |
| Atmosphere (GPU) | [GPU Gems 2 Ch. 16, "Accurate Atmospheric Scattering" (O'Neil)](https://developer.nvidia.com/gpugems/gpugems2/part-ii-shading-lighting-and-shadows/chapter-16-accurate-atmospheric-scattering) |
| Erosion | [Hans Theobald Beyer, "Implementation of a method for hydraulic erosion"](https://www.firespark.de/resources/downloads/implementation%20of%20a%20methode%20for%20hydraulic%20erosion.pdf) |
| Procedural terrain | [GPU Gems 3 Ch. 1, "Generating Complex Procedural Terrains Using the GPU"](https://developer.nvidia.com/gpugems/gpugems3/part-i-geometry/chapter-1-generating-complex-procedural-terrains-using-gpu) |
| Water / Gerstner | [GPU Gems 1 Ch. 1, "Effective Water Simulation from Physical Models"](https://developer.nvidia.com/gpugems/gpugems/part-i-natural-effects/chapter-1-effective-water-simulation-physical-models) |
| Ocean rendering | [Tessendorf 2001, "Simulating Ocean Water"](https://people.computing.clemson.edu/~jtessen/reports/papers_files/coursenotes2004.pdf) |
| Virtual texturing | [Mittring 2008, "Advanced Virtual Texture Topics"](https://advances.realtimerendering.com/s2008/Mittring-AdvancedVirtualTextureTechniques.pdf) |
| LOD | [Strugar 2010, "CDLOD: Continuous Distance-Dependent Level of Detail"](https://aggrobird.com/files/cdlod_latest.pdf) |
| Horizon mapping | [Sloan & Cohen 2000, "Interactive Horizon Mapping"](https://www.ppsloan.org/publications/bs.pdf) |
| Self-shadowing | [Snyder & Nowrouzezahrai 2008, "Fast Soft Self-Shadowing on Dynamic Height Fields"](https://cim.mcgill.ca/~derek/files/hfvisib.pdf) |
| 64-bit rendering | [Cozzi & Ring, "3D Engine Design for Virtual Globes"](https://www.virtualglobebook.com/) |
| 64-bit GPU | [Godot, "Emulating Double Precision on the GPU"](https://godotengine.org/article/emulating-double-precision-gpu-render-large-worlds/) |
| Log depth | [Outerra blog, "Logarithmic Depth Buffer"](https://outerra.blogspot.com/2012/11/maximizing-depth-buffer-range-and.html) |
| Noise | [FastNoiseLite](https://github.com/Auburn/FastNoiseLite) |
