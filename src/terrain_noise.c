#define FNL_IMPL
#include "terrain_noise.h"
#include <math.h>

static float smoothstepf(float edge0, float edge1, float x) {
    float t = (x - edge0) / (edge1 - edge0);
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return t * t * (3.0f - 2.0f * t);
}

void terrain_noise_init(TerrainNoise* tn, int seed) {
    // Continental: large landmass shapes
    tn->continental = fnlCreateState();
    tn->continental.seed = seed;
    tn->continental.noise_type = FNL_NOISE_OPENSIMPLEX2;
    tn->continental.fractal_type = FNL_FRACTAL_FBM;
    tn->continental.frequency = 0.6f;
    tn->continental.octaves = 3;

    // Mountain: sharp ridged peaks
    tn->mountain = fnlCreateState();
    tn->mountain.seed = seed + 4000;
    tn->mountain.noise_type = FNL_NOISE_OPENSIMPLEX2;
    tn->mountain.fractal_type = FNL_FRACTAL_RIDGED;
    tn->mountain.frequency = 1.5f;
    tn->mountain.octaves = 5;

    // Warp: domain warping for organic coastlines
    tn->warp = fnlCreateState();
    tn->warp.seed = seed + 1000;
    tn->warp.noise_type = FNL_NOISE_OPENSIMPLEX2;
    tn->warp.fractal_type = FNL_FRACTAL_FBM;
    tn->warp.frequency = 4.0f;
    tn->warp.octaves = 3;

    // Detail: fine surface variation
    tn->detail = fnlCreateState();
    tn->detail.seed = seed + 2000;
    tn->detail.noise_type = FNL_NOISE_OPENSIMPLEX2;
    tn->detail.fractal_type = FNL_FRACTAL_RIDGED;
    tn->detail.frequency = 16.0f;
    tn->detail.octaves = 3;
}

float terrain_noise_sample(const TerrainNoise* tn, float x, float y, float z) {
    // Domain warping
    float warp_strength = 0.5f;
    float wx = fnlGetNoise3D((fnl_state*)&tn->warp, x * 100.0f, y * 100.0f, z * 100.0f + 1000.0f);
    float wy = fnlGetNoise3D((fnl_state*)&tn->warp, x * 100.0f + 3000.0f, y * 100.0f, z * 100.0f);
    float wz = fnlGetNoise3D((fnl_state*)&tn->warp, x * 100.0f, y * 100.0f + 5000.0f, z * 100.0f);
    float sx = x + wx * warp_strength * 0.01f;
    float sy = y + wy * warp_strength * 0.01f;
    float sz = z + wz * warp_strength * 0.01f;

    // Continental noise
    float continent = fnlGetNoise3D((fnl_state*)&tn->continental, sx * 100.0f, sy * 100.0f, sz * 100.0f);

    // Mountain noise — remap and square for sharp peaks
    float mountain = fnlGetNoise3D((fnl_state*)&tn->mountain, sx * 100.0f, sy * 100.0f, sz * 100.0f);
    mountain = (mountain + 1.0f) * 0.5f;  // [-1,1] -> [0,1]
    mountain = mountain * mountain;         // square for sharp peaks

    // Mountain mask: only on interior land
    float land_factor = smoothstepf(-0.05f, 0.35f, continent);

    // Detail noise
    float detail = fnlGetNoise3D((fnl_state*)&tn->detail, sx * 100.0f, sy * 100.0f, sz * 100.0f);

    // Compose
    float height = continent * 0.55f + mountain * land_factor * 0.45f + detail * land_factor * 0.05f;

    // Power redistribution
    if (height > 0.0f) {
        height = powf(height, 1.35f);   // steepen mountains
    } else {
        height = -powf(-height, 0.8f);  // flatten ocean floors
    }

    // Clamp to [-1, 1]
    if (height > 1.0f) height = 1.0f;
    if (height < -1.0f) height = -1.0f;

    return height;
}

float terrain_height_to_meters(float normalized_height) {
    return TERRAIN_MIN_M + (normalized_height + 1.0f) * 0.5f * TERRAIN_AMPLITUDE_M;
}

float terrain_sample_height_m(const TerrainNoise* tn, float x, float y, float z) {
    float n = terrain_noise_sample(tn, x, y, z);
    return terrain_height_to_meters(n);
}

static HMM_Vec3 lerp_color(HMM_Vec3 a, HMM_Vec3 b, float t) {
    return HMM_V3(
        a.X + (b.X - a.X) * t,
        a.Y + (b.Y - a.Y) * t,
        a.Z + (b.Z - a.Z) * t
    );
}

HMM_Vec3 terrain_biome_color(float height_m, float slope) {
    float above_sea = height_m - TERRAIN_SEA_LEVEL_M;

    // Underwater
    if (above_sea < -200.0f) return BIOME_COLOR_WATER_DEEP;
    if (above_sea < 0.0f) {
        float t = smoothstepf(-200.0f, 0.0f, above_sea);
        return lerp_color(BIOME_COLOR_WATER_DEEP, BIOME_COLOR_WATER_SHALLOW, t);
    }

    // Shore to sand
    if (above_sea < 200.0f) {
        float t = smoothstepf(0.0f, 200.0f, above_sea);
        return lerp_color(BIOME_COLOR_WATER_SHALLOW, BIOME_COLOR_SAND, t);
    }

    // Sand to grass
    if (above_sea < 400.0f) {
        float t = smoothstepf(200.0f, 400.0f, above_sea);
        return lerp_color(BIOME_COLOR_SAND, BIOME_COLOR_GRASS, t);
    }

    // Grass to rock (slope accelerates transition)
    float rock_start = 1500.0f - slope * 500.0f;
    if (above_sea < rock_start) return BIOME_COLOR_GRASS;

    float rock_end = rock_start + 300.0f;
    if (above_sea < rock_end) {
        float t = smoothstepf(rock_start, rock_end, above_sea);
        return lerp_color(BIOME_COLOR_GRASS, BIOME_COLOR_ROCK, t);
    }

    // Rock to snow
    if (above_sea < 4000.0f) return BIOME_COLOR_ROCK;
    if (above_sea < 4500.0f) {
        float t = smoothstepf(4000.0f, 4500.0f, above_sea);
        return lerp_color(BIOME_COLOR_ROCK, BIOME_COLOR_SNOW, t);
    }

    return BIOME_COLOR_SNOW;
}
