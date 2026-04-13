#ifndef TERRAIN_NOISE_H
#define TERRAIN_NOISE_H

#include "FastNoiseLite.h"
#include "HandmadeMath.h"

// Terrain configuration
#define TERRAIN_SEED         42
#define TERRAIN_SEA_LEVEL_M  4000.0f
#define TERRAIN_AMPLITUDE_M  8000.0f
#define TERRAIN_MIN_M        500.0f

// Biome color palette
#define BIOME_COLOR_WATER_DEEP   HMM_V3(0.05f, 0.10f, 0.30f)
#define BIOME_COLOR_WATER_SHALLOW HMM_V3(0.10f, 0.25f, 0.45f)
#define BIOME_COLOR_SAND         HMM_V3(0.76f, 0.70f, 0.50f)
#define BIOME_COLOR_GRASS        HMM_V3(0.20f, 0.50f, 0.15f)
#define BIOME_COLOR_ROCK         HMM_V3(0.45f, 0.42f, 0.38f)
#define BIOME_COLOR_SNOW         HMM_V3(0.90f, 0.92f, 0.95f)

typedef struct {
    fnl_state continental;
    fnl_state mountain;
    fnl_state warp;
    fnl_state detail;
} TerrainNoise;

// Initialize all noise generators with seed
void terrain_noise_init(TerrainNoise* tn, int seed);

// Sample normalized height [-1, 1] at a point on the unit sphere
float terrain_noise_sample(const TerrainNoise* tn, float x, float y, float z);

// Convert normalized height to meters
float terrain_height_to_meters(float normalized_height);

// Get biome color for a given height in meters and slope
HMM_Vec3 terrain_biome_color(float height_m, float slope);

// Sample height in meters at a point on the unit sphere
float terrain_sample_height_m(const TerrainNoise* tn, float x, float y, float z);

#endif
