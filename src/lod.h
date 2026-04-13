#ifndef LOD_H
#define LOD_H

#include "HandmadeMath.h"
#include "sokol_gfx.h"
#include "job_system.h"
#include "terrain_noise.h"
#include <stdbool.h>

#define LOD_ROOT_COUNT      20     // icosahedron faces
#define LOD_CHILDREN        4      // aperture-4 subdivision
#define LOD_MAX_DEPTH       13
#define LOD_MAX_NODES       16384
#define LOD_SPLIT_FACTOR    8.0f
#define LOD_MAX_SPLITS      256    // per frame
#define LOD_MAX_UPLOADS     64     // GPU uploads per frame
#define LOD_NUM_WORKERS     4
#define LOD_VERTS_PER_EDGE  17     // vertices along each triangle edge

// Vertex format for LOD mesh
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
    int parent;
    int children[LOD_CHILDREN];
    int depth;
    LodNodeState state;

    // Triangle corners on unit sphere
    HMM_Vec3 v0, v1, v2;
    HMM_Vec3 center;          // centroid on unit sphere
    float arc;                // angular size of patch

    // Mesh data (CPU side, filled by worker thread)
    LodVertex* vertices;
    int vertex_count;
    uint16_t* indices;
    int index_count;

    // GPU buffers
    sg_buffer vbuf;
    sg_buffer ibuf;
    bool gpu_valid;
} LodNode;

typedef struct {
    LodNode nodes[LOD_MAX_NODES];
    int node_count;
    int roots[LOD_ROOT_COUNT];

    float split_factor;
    float depth_arc[LOD_MAX_DEPTH + 1]; // precomputed arc per depth

    // Floating origin (double precision)
    double world_origin[3];
    float planet_radius;

    // Terrain
    TerrainNoise terrain;

    // Threading
    JobSystem* jobs;

    // Per-frame budgets
    int splits_this_frame;
    int uploads_this_frame;
} LodTree;

void lod_tree_init(LodTree* tree, float planet_radius, int seed);
void lod_tree_destroy(LodTree* tree);
void lod_tree_update(LodTree* tree, const double camera_pos_d[3]);
void lod_tree_render(const LodTree* tree, sg_pipeline pip,
                     HMM_Mat4 view_proj, HMM_Vec4 cam_offset,
                     HMM_Vec4 cam_offset_low, HMM_Vec4 log_depth,
                     HMM_Vec4 sun_dir, HMM_Vec4 cam_pos,
                     HMM_Vec4 atmos_params);

#endif
