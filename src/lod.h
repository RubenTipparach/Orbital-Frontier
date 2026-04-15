#ifndef LOD_H
#define LOD_H

#include "HandmadeMath.h"
#include "sokol_gfx.h"
#include "job_system.h"
#include "terrain_noise.h"
#include <stdbool.h>

#define LOD_ROOT_COUNT      20
#define LOD_CHILDREN        4
#define LOD_MAX_DEPTH       13
#define LOD_MAX_NODES       16384
#define LOD_SPLIT_FACTOR    8.0f
#define LOD_MAX_SPLITS      256
#define LOD_MAX_UPLOADS     64
#define LOD_NUM_WORKERS     4

// Recenter floating origin when camera drifts this far (meters)
// Set very large — only recenter when float precision is actually degrading.
// At 2M meters offset, float32 still has ~0.25m precision (fine for LOD patches).
// Only recenter when we'd lose >1m precision (~16M meters).
#define ORIGIN_RECENTER_THRESHOLD 16000000.0

// Vertex format for LOD mesh (36 bytes, matches hex-planets)
typedef struct {
    float pos[3];
    float normal[3];
    float color[3];
} LodVertex;

typedef enum {
    LOD_UNLOADED = 0,
    LOD_GENERATING,
    LOD_READY,
    LOD_ACTIVE,
} LodNodeState;

typedef struct {
    // Triangle on unit sphere
    HMM_Vec3 v0, v1, v2;
    HMM_Vec3 center;           // centroid on unit sphere (normalized)
    float angular_radius;       // angular radius of patch
} LodTriangle;

typedef struct {
    int parent;
    int children[LOD_CHILDREN];
    int depth;
    LodNodeState state;
    bool is_leaf;

    LodTriangle tri;

    // CPU mesh data (filled by worker, freed after GPU upload)
    LodVertex* vertices;
    int vertex_count;

    // GPU buffer
    sg_buffer gpu_buffer;
    int gpu_vertex_count;
    bool gpu_valid;
} LodNode;

typedef struct {
    LodNode nodes[LOD_MAX_NODES];
    int node_count;
    int roots[LOD_ROOT_COUNT];

    float split_factor;
    float depth_arc[LOD_MAX_DEPTH + 1];
    int max_depth_effective;  // actual max subdivision (capped, no hex terrain = lower)

    // Floating origin (double precision)
    double world_origin[3];
    float planet_radius;
    int seed;

    // Camera state (updated each frame, relative to world_origin)
    HMM_Vec3 camera_pos;

    // Terrain
    TerrainNoise terrain;

    // Threading
    JobSystem* jobs;

    // Per-frame budgets
    int splits_this_frame;
    int uploads_this_frame;

    // Per-depth stats (collected each frame)
    struct {
        int patch_count;
        int vertex_count;
    } level_stats[LOD_MAX_DEPTH + 1];

    // Debug
    bool show_lod_debug;  // L key: color patches by depth

    // Render stats (set during render)
    int active_leaf_count;    // draw calls this frame
    int total_vertex_count;   // total verts drawn
} LodTree;

void lod_tree_init(LodTree* tree, float planet_radius, int seed);
void lod_tree_destroy(LodTree* tree);
void lod_tree_update(LodTree* tree, const double camera_pos_d[3]);
bool lod_tree_update_origin(LodTree* tree, const double camera_pos_d[3]);
void lod_tree_render(const LodTree* tree, sg_pipeline pip,
                     HMM_Mat4 view_proj, HMM_Vec4 cam_offset,
                     HMM_Vec4 cam_offset_low, HMM_Vec4 log_depth,
                     HMM_Vec4 sun_dir, HMM_Vec4 cam_pos,
                     HMM_Vec4 atmos_params);

#endif
