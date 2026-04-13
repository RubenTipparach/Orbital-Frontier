# Orbital-Frontier

## Architecture
- C + Sokol graphics engine for cross-platform planet rendering
- This project does NOT use hexagon/hex-grid logic — use simple triangle-based LOD and meshes only
- Reference engine: [Caelum](https://github.com/RubenTipparach/Caelum) in `hex-planets/` subfolder (hex logic there is legacy, not carried forward)

## Build
- CMake build system
- Backends: D3D11 (Windows), Metal (macOS), OpenGL (Linux), WebGL2/WebGPU (Web)
- Shader cross-compilation via sokol-shdc

## Key Documents
- `planetary.md` — rendering pipeline reference
- `physics_engine.md` — n-body physics and orbital mechanics
- `gameplay.md` — rocket building, orbital maneuvers, docking
