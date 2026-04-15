#ifndef TERRAIN_NOISE_H
#define TERRAIN_NOISE_H

// Shared terrain noise — matches hex-planets/Caelum terrain_noise.h exactly.
// Header-only inline functions guarantee identical sampling across LOD and future systems.

#include "HandmadeMath.h"
#include "FastNoiseLite.h"
#include <math.h>

// ---- Terrain height constants ----
#define TERRAIN_SEA_LEVEL_M   4000.0f
#define TERRAIN_AMPLITUDE_M   8000.0f
#define TERRAIN_MIN_M         500.0f

typedef struct {
    fnl_state continental;
    fnl_state mountain;
    fnl_state warp;
    fnl_state detail;
    fnl_state color_noise;  // for color perturbation
} TerrainNoise;

static inline float ht_smoothstepf(float edge0, float edge1, float x) {
    float t = (x - edge0) / (edge1 - edge0);
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return t * t * (3.0f - 2.0f * t);
}

static inline HMM_Vec3 ht_vec3_lerp(HMM_Vec3 a, HMM_Vec3 b, float t) {
    return HMM_V3(a.X + (b.X - a.X) * t, a.Y + (b.Y - a.Y) * t, a.Z + (b.Z - a.Z) * t);
}

// Initialize all noise generators from seed
static inline void terrain_noise_init(TerrainNoise* tn, int seed) {
    tn->continental = fnlCreateState();
    tn->continental.noise_type = FNL_NOISE_OPENSIMPLEX2;
    tn->continental.fractal_type = FNL_FRACTAL_FBM;
    tn->continental.octaves = 3;
    tn->continental.frequency = 0.6f;
    tn->continental.seed = seed;

    tn->mountain = fnlCreateState();
    tn->mountain.noise_type = FNL_NOISE_OPENSIMPLEX2;
    tn->mountain.fractal_type = FNL_FRACTAL_RIDGED;
    tn->mountain.octaves = 5;
    tn->mountain.frequency = 1.5f;
    tn->mountain.seed = seed + 4000;

    tn->warp = fnlCreateState();
    tn->warp.noise_type = FNL_NOISE_OPENSIMPLEX2;
    tn->warp.fractal_type = FNL_FRACTAL_FBM;
    tn->warp.octaves = 3;
    tn->warp.frequency = 4.0f;
    tn->warp.seed = seed + 1000;

    tn->detail = fnlCreateState();
    tn->detail.noise_type = FNL_NOISE_OPENSIMPLEX2;
    tn->detail.fractal_type = FNL_FRACTAL_RIDGED;
    tn->detail.octaves = 3;
    tn->detail.frequency = 16.0f;
    tn->detail.seed = seed + 2000;

    tn->color_noise = fnlCreateState();
    tn->color_noise.noise_type = FNL_NOISE_OPENSIMPLEX2;
    tn->color_noise.fractal_type = FNL_FRACTAL_FBM;
    tn->color_noise.octaves = 2;
    tn->color_noise.frequency = 8.0f;
    tn->color_noise.seed = seed + 7000;
}

// Sample normalized terrain height [-1, 1] at a point on the unit sphere.
// Matches hex-planets ht_sample_terrain_noise() exactly.
static inline float terrain_noise_sample(TerrainNoise* tn, float x, float y, float z) {
    float scale = 3.0f;
    float px = x * scale;
    float py = y * scale;
    float pz = z * scale;

    // Domain warping with magic offsets (matches hex-planets)
    float warp_strength = 0.5f;
    float wx = fnlGetNoise3D(&tn->warp, px + 5.2f, py + 1.3f, pz + 3.7f);
    float wy = fnlGetNoise3D(&tn->warp, px + 9.1f, py + 4.8f, pz + 7.2f);
    float wz = fnlGetNoise3D(&tn->warp, px + 2.6f, py + 8.4f, pz + 0.9f);
    float wpx = px + wx * warp_strength;
    float wpy = py + wy * warp_strength;
    float wpz = pz + wz * warp_strength;

    // Continental
    float continent = fnlGetNoise3D(&tn->continental, wpx, wpy, wpz);

    // Mountain — remap [-1,1]→[0,1], square for sharp peaks, gate by land factor
    float mountain_raw = fnlGetNoise3D(&tn->mountain, wpx, wpy, wpz);
    float mountain_val = (mountain_raw + 1.0f) * 0.5f;
    mountain_val *= mountain_val;
    float land_factor = ht_smoothstepf(-0.05f, 0.35f, continent);
    float mountain_height = mountain_val * land_factor;

    // Detail
    float detail_val = fnlGetNoise3D(&tn->detail, px, py, pz);
    float detail_weight = 0.05f + land_factor * 0.10f;

    // Compose (matches hex-planets: the -0.22 creates more ocean)
    float height = continent * 0.55f - 0.22f;
    height += mountain_height * 0.45f;
    height += detail_val * detail_weight;

    // Power redistribution
    if (height > 0.0f) {
        height = powf(height, 1.35f);
    } else {
        height = -powf(-height, 0.8f);
    }

    if (height > 1.0f) height = 1.0f;
    if (height < -1.0f) height = -1.0f;
    return height;
}

// Convert normalized height to meters
static inline float terrain_height_to_meters(float normalized_height) {
    float h = TERRAIN_MIN_M + (normalized_height + 1.0f) * 0.5f * TERRAIN_AMPLITUDE_M;
    if (h < 0.0f) h = 0.0f;
    return h;
}

// Sample height in meters at a point on the unit sphere
static inline float terrain_sample_height_m(TerrainNoise* tn, float x, float y, float z) {
    float n = terrain_noise_sample(tn, x, y, z);
    return terrain_height_to_meters(n);
}

// Biome color from height in meters (matches hex-planets terrain_color_m)
static inline HMM_Vec3 terrain_biome_color(float height_m) {
    float rel = height_m - TERRAIN_SEA_LEVEL_M;

    const HMM_Vec3 water_deep    = HMM_V3(0.06f, 0.10f, 0.25f);
    const HMM_Vec3 water_shallow = HMM_V3(0.12f, 0.21f, 0.39f);
    const HMM_Vec3 sand_dark     = HMM_V3(0.78f, 0.68f, 0.58f);
    const HMM_Vec3 sand_light    = HMM_V3(0.96f, 0.88f, 0.80f);
    const HMM_Vec3 grass_dark    = HMM_V3(0.04f, 0.38f, 0.22f);
    const HMM_Vec3 grass_light   = HMM_V3(0.12f, 0.68f, 0.40f);
    const HMM_Vec3 rock_dark     = HMM_V3(0.32f, 0.31f, 0.30f);
    const HMM_Vec3 rock_light    = HMM_V3(0.55f, 0.54f, 0.53f);
    const HMM_Vec3 ice           = HMM_V3(0.44f, 0.77f, 0.97f);

    // Ocean
    if (height_m < TERRAIN_SEA_LEVEL_M) {
        float depth_t = -rel / 2000.0f;
        if (depth_t > 1.0f) depth_t = 1.0f;
        return ht_vec3_lerp(water_shallow, water_deep, depth_t);
    }

    // Land biome transitions with ±100m blend zones
    float blend = 100.0f;
    float t1 = ht_smoothstepf(200.0f  - blend, 200.0f  + blend, rel);
    float t2 = ht_smoothstepf(1500.0f - blend, 1500.0f + blend, rel);
    float t3 = ht_smoothstepf(3000.0f - blend, 3000.0f + blend, rel);
    float t4 = ht_smoothstepf(4500.0f - blend, 4500.0f + blend, rel);

    // Local height within biome band for dark→light gradient
    float local_h;
    if (rel < 200.0f)       local_h = rel / 200.0f;
    else if (rel < 1500.0f) local_h = (rel - 200.0f) / 1300.0f;
    else if (rel < 4500.0f) local_h = (rel - 1500.0f) / 3000.0f;
    else                    { local_h = (rel - 4500.0f) / 1000.0f; if (local_h > 1.0f) local_h = 1.0f; }

    HMM_Vec3 sand  = ht_vec3_lerp(sand_dark,  sand_light,  local_h);
    HMM_Vec3 grass = ht_vec3_lerp(grass_dark, grass_light, local_h);
    HMM_Vec3 rock  = ht_vec3_lerp(rock_dark,  rock_light,  local_h);

    HMM_Vec3 color = sand;
    color = ht_vec3_lerp(color, grass, t1);
    color = ht_vec3_lerp(color, rock,  t2);
    color = ht_vec3_lerp(color, rock,  t3);
    color = ht_vec3_lerp(color, ice,   t4);
    return color;
}

// Color perturbation noise (matches hex-planets perturb_color)
static inline HMM_Vec3 terrain_perturb_color(HMM_Vec3 base, TerrainNoise* tn, float x, float y, float z) {
    float cn = fnlGetNoise3D(&tn->color_noise, x * 3.0f, y * 3.0f, z * 3.0f);
    float variation = cn * 0.12f;
    float r = base.X + variation;
    float g = base.Y + variation * 0.8f;
    float b = base.Z + variation * 0.6f;
    if (r < 0.0f) r = 0.0f; if (r > 1.0f) r = 1.0f;
    if (g < 0.0f) g = 0.0f; if (g > 1.0f) g = 1.0f;
    if (b < 0.0f) b = 0.0f; if (b > 1.0f) b = 1.0f;
    return HMM_V3(r, g, b);
}

#endif
