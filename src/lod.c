#include "lod.h"
#include "debug_log.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

// ---- Icosahedron base geometry ----

static const float ICO_VERTS[12][3] = {
    { 0.000f,  1.000f,  0.000f},
    { 0.894f,  0.447f,  0.000f},
    { 0.276f,  0.447f,  0.851f},
    {-0.724f,  0.447f,  0.526f},
    {-0.724f,  0.447f, -0.526f},
    { 0.276f,  0.447f, -0.851f},
    { 0.724f, -0.447f,  0.526f},
    {-0.276f, -0.447f,  0.851f},
    {-0.894f, -0.447f,  0.000f},
    {-0.276f, -0.447f, -0.851f},
    { 0.724f, -0.447f, -0.526f},
    { 0.000f, -1.000f,  0.000f},
};

static const int ICO_FACES[20][3] = {
    {0,2,1}, {0,3,2}, {0,4,3}, {0,5,4}, {0,1,5},
    {1,2,6}, {2,3,7}, {3,4,8}, {4,5,9}, {5,1,10},
    {6,2,7}, {7,3,8}, {8,4,9}, {9,5,10}, {10,1,6},
    {11,6,7}, {11,7,8}, {11,8,9}, {11,9,10}, {11,10,6},
};

static HMM_Vec3 ico_vert(int i) {
    return HMM_NormV3(HMM_V3(ICO_VERTS[i][0], ICO_VERTS[i][1], ICO_VERTS[i][2]));
}

// ---- Helper math ----

static float vec3_dot(HMM_Vec3 a, HMM_Vec3 b) {
    return a.X * b.X + a.Y * b.Y + a.Z * b.Z;
}

static HMM_Vec3 vec3_scale(HMM_Vec3 v, float s) {
    return HMM_V3(v.X * s, v.Y * s, v.Z * s);
}

static HMM_Vec3 vec3_add(HMM_Vec3 a, HMM_Vec3 b) {
    return HMM_V3(a.X + b.X, a.Y + b.Y, a.Z + b.Z);
}

static HMM_Vec3 vec3_sub(HMM_Vec3 a, HMM_Vec3 b) {
    return HMM_V3(a.X - b.X, a.Y - b.Y, a.Z - b.Z);
}

static HMM_Vec3 vec3_normalize(HMM_Vec3 v) {
    float len = sqrtf(v.X * v.X + v.Y * v.Y + v.Z * v.Z);
    if (len < 1e-8f) return HMM_V3(0, 1, 0);
    return HMM_V3(v.X / len, v.Y / len, v.Z / len);
}

static HMM_Vec3 vec3_cross(HMM_Vec3 a, HMM_Vec3 b) {
    return HMM_V3(a.Y*b.Z - a.Z*b.Y, a.Z*b.X - a.X*b.Z, a.X*b.Y - a.Y*b.X);
}

static float clampf(float x, float lo, float hi) {
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

// ---- Node allocation ----

static int alloc_node(LodTree* tree) {
    if (tree->node_count >= LOD_MAX_NODES) return -1;
    int idx = tree->node_count++;
    memset(&tree->nodes[idx], 0, sizeof(LodNode));
    tree->nodes[idx].parent = -1;
    tree->nodes[idx].is_leaf = true;
    for (int i = 0; i < LOD_CHILDREN; i++) tree->nodes[idx].children[i] = -1;
    return idx;
}

static void free_node_gpu(LodNode* node) {
    if (node->gpu_valid) {
        sg_destroy_buffer(node->gpu_buffer);
        node->gpu_valid = false;
        node->gpu_vertex_count = 0;
    }
    free(node->vertices);
    node->vertices = NULL;
    node->vertex_count = 0;
    node->state = LOD_UNLOADED;
}

// ---- Distance metric (matches hex-planets) ----

static float patch_center_distance(const LodTree* tree, const LodNode* node) {
    float cam_r = sqrtf(vec3_dot(tree->camera_pos, tree->camera_pos));
    if (cam_r < 1.0f) cam_r = 1.0f;
    HMM_Vec3 cam_dir = vec3_scale(tree->camera_pos, 1.0f / cam_r);

    float cos_angle = vec3_dot(cam_dir, node->tri.center);
    cos_angle = clampf(cos_angle, -1.0f, 1.0f);
    float angle_to_center = acosf(cos_angle);
    float arc_dist = angle_to_center * tree->planet_radius;

    // Altitude penalty prevents over-splitting from orbit
    float max_surface_r = tree->planet_radius + TERRAIN_AMPLITUDE_M;
    float altitude = cam_r - max_surface_r;
    if (altitude < 0.0f) altitude = 0.0f;

    return sqrtf(arc_dist * arc_dist + altitude * altitude);
}

// ---- Back-hemisphere culling ----

static bool patch_on_back_hemisphere(const LodTree* tree, const LodNode* node) {
    float cam_r = sqrtf(vec3_dot(tree->camera_pos, tree->camera_pos));
    if (cam_r < 1.0f) return false;
    HMM_Vec3 cam_dir = vec3_scale(tree->camera_pos, 1.0f / cam_r);
    float horizon_angle = acosf(fminf(1.0f, tree->planet_radius / cam_r));
    float cos_angle = vec3_dot(cam_dir, node->tri.center);
    cos_angle = clampf(cos_angle, -1.0f, 1.0f);
    float angle_to_center = acosf(cos_angle);
    float nearest_angle = angle_to_center - node->tri.angular_radius;
    return nearest_angle > ((float)M_PI / 2.0f + horizon_angle + 0.1f);
}

// ---- Split/merge decisions (matches hex-planets: 2x hysteresis) ----

static bool should_split(const LodTree* tree, const LodNode* node) {
    if (node->depth >= tree->max_depth_effective) return false;
    // Don't split TO max depth (matches hex-planets)
    if (node->depth + 1 >= tree->max_depth_effective) return false;
    float dist = patch_center_distance(tree, node);
    float patch_arc = tree->depth_arc[node->depth];
    return dist < patch_arc * tree->split_factor;
}

static bool should_merge(const LodTree* tree, const LodNode* node) {
    float dist = patch_center_distance(tree, node);
    float patch_arc = tree->depth_arc[node->depth];
    return dist > patch_arc * tree->split_factor * 2.0f;
}

// ---- Tessellation level per depth (matches hex-planets) ----

static int tess_for_depth(int depth) {
    if (depth >= 12) return 32;
    if (depth >= 11) return 24;
    if (depth >= 10) return 16;
    if (depth >= 8)  return 8;
    if (depth >= 4)  return 6;
    return 4;
}

// ---- Mesh generation ----

typedef struct {
    int node_idx;
    LodTree* tree;
    HMM_Vec3 v0, v1, v2;
    int depth;
    float planet_radius;
    double world_origin[3];
    TerrainNoise terrain;
    int seed;
    // Result (written by worker, read by main thread)
    LodVertex* result_verts;
    int result_count;
    volatile int completed;  // 0 = running, 1 = done
} MeshGenJob;

#define MAX_PENDING_JOBS 256
static MeshGenJob* g_pending_jobs[MAX_PENDING_JOBS];
static int g_pending_job_count = 0;

static void generate_mesh(void* data) {
    MeshGenJob* job = (MeshGenJob*)data;

    int tess = tess_for_depth(job->depth);
    int vert_per_row_sum = 0;
    for (int r = 0; r <= tess; r++) vert_per_row_sum += (tess - r + 1);
    int max_verts = vert_per_row_sum; // unique grid points
    int max_tris = tess * tess;       // triangle count for tessellated triangle
    int max_out = max_tris * 3;       // 3 verts per triangle (non-indexed)

    // First pass: compute grid positions, normals (sphere direction), colors
    HMM_Vec3* points = (HMM_Vec3*)calloc(max_verts, sizeof(HMM_Vec3));
    HMM_Vec3* normals = (HMM_Vec3*)calloc(max_verts, sizeof(HMM_Vec3));
    HMM_Vec3* colors = (HMM_Vec3*)calloc(max_verts, sizeof(HMM_Vec3));

    int idx = 0;
    for (int row = 0; row <= tess; row++) {
        for (int col = 0; col <= tess - row; col++) {
            float u = (float)col / (float)tess;
            float v = (float)row / (float)tess;
            float w = 1.0f - u - v;

            // Barycentric interpolation on unit sphere
            HMM_Vec3 p = vec3_add(vec3_add(
                vec3_scale(job->v0, w),
                vec3_scale(job->v1, u)),
                vec3_scale(job->v2, v));
            p = vec3_normalize(p);

            // Sample terrain (matches hex-planets exactly)
            float h_m = terrain_sample_height_m(&job->terrain, p.X, p.Y, p.Z);
            // Effective height: clamp to sea level (water surface is flat)
            float effective_h_m = h_m;
            if (effective_h_m < TERRAIN_SEA_LEVEL_M) effective_h_m = TERRAIN_SEA_LEVEL_M;
            float radius = job->planet_radius + effective_h_m;

            // World position relative to floating origin
            double wx = (double)p.X * (double)radius - job->world_origin[0];
            double wy = (double)p.Y * (double)radius - job->world_origin[1];
            double wz = (double)p.Z * (double)radius - job->world_origin[2];

            points[idx] = HMM_V3((float)wx, (float)wy, (float)wz);
            normals[idx] = p;

            // Biome color from raw height + perturbation noise
            HMM_Vec3 base_color = terrain_biome_color(h_m);
            colors[idx] = terrain_perturb_color(base_color, &job->terrain, p.X, p.Y, p.Z);
            idx++;
        }
    }

    // Second pass: emit triangles (non-indexed, 3 verts per tri)
    LodVertex* verts = (LodVertex*)calloc(max_out, sizeof(LodVertex));
    int vi = 0;

    // Build lookup table for grid row offsets
    int* grid_offset = (int*)calloc(tess + 2, sizeof(int));
    grid_offset[0] = 0;
    for (int r = 0; r <= tess; r++) {
        grid_offset[r + 1] = grid_offset[r] + (tess - r + 1);
    }

    // Emit triangle helper: writes 3 verts, checks winding against outward direction
    #define EMIT_TRI(a, b, c) do { \
        HMM_Vec3 pa = points[a], pb = points[b], pc = points[c]; \
        HMM_Vec3 e1 = vec3_sub(pb, pa); \
        HMM_Vec3 e2 = vec3_sub(pc, pa); \
        HMM_Vec3 fn = vec3_cross(e1, e2); \
        /* Face center direction (approximate outward) */ \
        HMM_Vec3 fc = vec3_add(vec3_add(normals[a], normals[b]), normals[c]); \
        /* If face normal points inward, swap b and c to fix winding */ \
        int t0 = (a), t1 = (b), t2 = (c); \
        if (vec3_dot(fn, fc) < 0.0f) { int tmp = t1; t1 = t2; t2 = tmp; } \
        int tri_idx[3] = { t0, t1, t2 }; \
        for (int k = 0; k < 3; k++) { \
            int gi = tri_idx[k]; \
            verts[vi].pos[0] = points[gi].X; \
            verts[vi].pos[1] = points[gi].Y; \
            verts[vi].pos[2] = points[gi].Z; \
            verts[vi].normal[0] = normals[gi].X; \
            verts[vi].normal[1] = normals[gi].Y; \
            verts[vi].normal[2] = normals[gi].Z; \
            verts[vi].color[0] = colors[gi].X; \
            verts[vi].color[1] = colors[gi].Y; \
            verts[vi].color[2] = colors[gi].Z; \
            vi++; \
        } \
    } while(0)

    for (int row = 0; row < tess; row++) {
        int cols = tess - row;
        for (int col = 0; col < cols; col++) {
            int i0 = grid_offset[row] + col;
            int i1 = grid_offset[row] + col + 1;
            int i2 = grid_offset[row + 1] + col;

            EMIT_TRI(i0, i1, i2);

            if (col < cols - 1) {
                int j0 = grid_offset[row] + col + 1;
                int j1 = grid_offset[row + 1] + col + 1;
                int j2 = grid_offset[row + 1] + col;
                EMIT_TRI(j0, j1, j2);
            }
        }
    }
    #undef EMIT_TRI

    free(points);
    free(normals);
    free(colors);
    free(grid_offset);

    // Store result in job (main thread will transfer to node)
    job->result_verts = verts;
    job->result_count = vi;
    // Memory barrier: ensure result_verts/count are visible before completed flag
#ifdef _WIN32
    MemoryBarrier();
#else
    __sync_synchronize();
#endif
    job->completed = 1;
}

static void request_mesh(LodTree* tree, int node_idx) {
    LodNode* node = &tree->nodes[node_idx];
    if (node->state != LOD_UNLOADED) return;
    if (g_pending_job_count >= MAX_PENDING_JOBS) return;

    MeshGenJob* job = (MeshGenJob*)calloc(1, sizeof(MeshGenJob));
    job->node_idx = node_idx;
    job->tree = tree;
    job->v0 = node->tri.v0;
    job->v1 = node->tri.v1;
    job->v2 = node->tri.v2;
    job->depth = node->depth;
    job->planet_radius = tree->planet_radius;
    job->seed = tree->seed;
    memcpy(job->world_origin, tree->world_origin, sizeof(double) * 3);
    job->terrain = tree->terrain;
    job->result_verts = NULL;
    job->result_count = 0;
    job->completed = 0;

    g_pending_jobs[g_pending_job_count++] = job;
    node->state = LOD_GENERATING;
    job_system_submit(tree->jobs, generate_mesh, job);
}

// Process completed mesh gen jobs (main thread only — no data race)
static void process_completed_jobs(void) {
    int write = 0;
    for (int i = 0; i < g_pending_job_count; i++) {
        MeshGenJob* job = g_pending_jobs[i];
        if (job->completed) {
            LodNode* node = &job->tree->nodes[job->node_idx];
            if (node->state == LOD_GENERATING) {
                node->vertices = job->result_verts;
                node->vertex_count = job->result_count;
                node->state = LOD_READY;
            } else {
                // Node was invalidated (origin recenter) while generating
                free(job->result_verts);
            }
            free(job);
        } else {
            g_pending_jobs[write++] = job;
        }
    }
    g_pending_job_count = write;
}

static void upload_mesh(LodNode* node) {
    if (node->state != LOD_READY || !node->vertices || node->vertex_count <= 0) return;

    node->gpu_buffer = sg_make_buffer(&(sg_buffer_desc){
        .data = { .ptr = node->vertices, .size = node->vertex_count * sizeof(LodVertex) },
        .label = "lod-vbuf",
    });
    node->gpu_valid = true;
    node->gpu_vertex_count = node->vertex_count;
    node->state = LOD_ACTIVE;

    free(node->vertices);
    node->vertices = NULL;
}

// ---- Tree operations ----

static void init_triangle(LodTriangle* tri, HMM_Vec3 v0, HMM_Vec3 v1, HMM_Vec3 v2) {
    tri->v0 = v0;
    tri->v1 = v1;
    tri->v2 = v2;
    tri->center = vec3_normalize(vec3_add(vec3_add(v0, v1), v2));
    // Angular radius: max angle from center to any vertex
    float a0 = acosf(clampf(vec3_dot(tri->center, v0), -1.0f, 1.0f));
    float a1 = acosf(clampf(vec3_dot(tri->center, v1), -1.0f, 1.0f));
    float a2 = acosf(clampf(vec3_dot(tri->center, v2), -1.0f, 1.0f));
    tri->angular_radius = fmaxf(a0, fmaxf(a1, a2));
}

static void split_node(LodTree* tree, int node_idx) {
    LodNode* node = &tree->nodes[node_idx];
    if (!node->is_leaf) return;
    if (node->depth >= LOD_MAX_DEPTH) return;

    HMM_Vec3 m01 = vec3_normalize(vec3_scale(vec3_add(node->tri.v0, node->tri.v1), 0.5f));
    HMM_Vec3 m12 = vec3_normalize(vec3_scale(vec3_add(node->tri.v1, node->tri.v2), 0.5f));
    HMM_Vec3 m02 = vec3_normalize(vec3_scale(vec3_add(node->tri.v0, node->tri.v2), 0.5f));

    HMM_Vec3 child_verts[4][3] = {
        { node->tri.v0, m01, m02 },
        { m01, node->tri.v1, m12 },
        { m02, m12, node->tri.v2 },
        { m01, m12, m02 },
    };

    for (int i = 0; i < LOD_CHILDREN; i++) {
        int ci = alloc_node(tree);
        if (ci < 0) return;
        node = &tree->nodes[node_idx]; // re-fetch

        LodNode* child = &tree->nodes[ci];
        child->parent = node_idx;
        child->depth = node->depth + 1;
        child->is_leaf = true;
        init_triangle(&child->tri, child_verts[i][0], child_verts[i][1], child_verts[i][2]);
        node->children[i] = ci;
    }
    node->is_leaf = false;
}

static void merge_children(LodTree* tree, int node_idx) {
    LodNode* node = &tree->nodes[node_idx];
    for (int i = 0; i < LOD_CHILDREN; i++) {
        if (node->children[i] >= 0) {
            merge_children(tree, node->children[i]);
            free_node_gpu(&tree->nodes[node->children[i]]);
            tree->nodes[node->children[i]].is_leaf = true;
            node->children[i] = -1;
        }
    }
    node->is_leaf = true;
}

// ---- Update traversal ----

static void update_node(LodTree* tree, int node_idx) {
    LodNode* node = &tree->nodes[node_idx];

    // Back-hemisphere culling: skip patches entirely behind planet
    if (patch_on_back_hemisphere(tree, node)) return;

    // Ensure this node has a mesh (for fallback rendering)
    if (node->state == LOD_UNLOADED) {
        request_mesh(tree, node_idx);
    }
    if (node->state == LOD_READY && tree->uploads_this_frame < LOD_MAX_UPLOADS) {
        upload_mesh(node);
        tree->uploads_this_frame++;
    }

    if (node->is_leaf) {
        // Should we split? Only if this node has a GPU mesh (for fallback)
        if (should_split(tree, node) && node->gpu_valid
            && tree->splits_this_frame < LOD_MAX_SPLITS) {
            split_node(tree, node_idx);
            tree->splits_this_frame++;
            node = &tree->nodes[node_idx]; // re-fetch
            for (int i = 0; i < LOD_CHILDREN; i++) {
                if (node->children[i] >= 0)
                    request_mesh(tree, node->children[i]);
            }
        }
    } else {
        // Should we merge?
        if (should_merge(tree, node)) {
            merge_children(tree, node_idx);
            node = &tree->nodes[node_idx];
            if (node->state == LOD_UNLOADED)
                request_mesh(tree, node_idx);
        } else {
            // Recurse into children
            for (int i = 0; i < LOD_CHILDREN; i++) {
                if (node->children[i] >= 0)
                    update_node(tree, node->children[i]);
            }
        }
    }
}

// ---- Public API ----

void lod_tree_init(LodTree* tree, float planet_radius, int seed) {
    memset(tree, 0, sizeof(LodTree));
    tree->planet_radius = planet_radius;
    tree->seed = seed;
    tree->split_factor = LOD_SPLIT_FACTOR;
    // Without hex terrain, cap depth to avoid micro-patch explosion.
    // Depth 10 ≈ 100m patches at 800km radius.
    tree->max_depth_effective = 10;

    // Precompute arc per depth (average of root angular radii, halved each depth)
    float avg_ar = 0.0f;

    // Create root nodes first to compute average
    for (int i = 0; i < LOD_ROOT_COUNT; i++) {
        int ni = alloc_node(tree);
        tree->roots[i] = ni;

        LodNode* node = &tree->nodes[ni];
        node->depth = 0;
        init_triangle(&node->tri, ico_vert(ICO_FACES[i][0]),
                      ico_vert(ICO_FACES[i][1]), ico_vert(ICO_FACES[i][2]));
        avg_ar += node->tri.angular_radius;
    }
    avg_ar /= (float)LOD_ROOT_COUNT;

    for (int d = 0; d <= LOD_MAX_DEPTH; d++) {
        float ar = avg_ar;
        for (int i = 0; i < d; i++) ar *= 0.5f;
        tree->depth_arc[d] = ar * planet_radius;
    }

    // Init terrain
    terrain_noise_init(&tree->terrain, seed);

    // Create job system
    tree->jobs = job_system_create(LOD_NUM_WORKERS);

    // Request meshes for all root nodes
    for (int i = 0; i < LOD_ROOT_COUNT; i++) {
        request_mesh(tree, tree->roots[i]);
    }

    debug_log("LOD tree: %d roots, avg_ar=%.4f, depth_arc[0]=%.0f depth_arc[13]=%.2f",
              LOD_ROOT_COUNT, (double)avg_ar,
              (double)tree->depth_arc[0], (double)tree->depth_arc[13]);
}

void lod_tree_destroy(LodTree* tree) {
    job_system_flush(tree->jobs);
    for (int i = 0; i < tree->node_count; i++) {
        free_node_gpu(&tree->nodes[i]);
    }
    job_system_destroy(tree->jobs);
}

bool lod_tree_update_origin(LodTree* tree, const double camera_pos_d[3]) {
    double dx = camera_pos_d[0] - tree->world_origin[0];
    double dy = camera_pos_d[1] - tree->world_origin[1];
    double dz = camera_pos_d[2] - tree->world_origin[2];
    double dist_sq = dx * dx + dy * dy + dz * dz;

    if (dist_sq < ORIGIN_RECENTER_THRESHOLD * ORIGIN_RECENTER_THRESHOLD)
        return false;

    debug_log("Floating origin recenter: (%.0f,%.0f,%.0f) -> (%.0f,%.0f,%.0f)",
              tree->world_origin[0], tree->world_origin[1], tree->world_origin[2],
              camera_pos_d[0], camera_pos_d[1], camera_pos_d[2]);

    tree->world_origin[0] = camera_pos_d[0];
    tree->world_origin[1] = camera_pos_d[1];
    tree->world_origin[2] = camera_pos_d[2];

    // Wait for pending jobs to complete before invalidating
    job_system_flush(tree->jobs);
    process_completed_jobs();

    // Destroy all node GPU buffers and CPU data
    for (int i = 0; i < tree->node_count; i++) {
        free_node_gpu(&tree->nodes[i]);
    }

    // Reset node pool and rebuild root nodes from scratch
    tree->node_count = 0;
    for (int i = 0; i < LOD_ROOT_COUNT; i++) {
        int ni = alloc_node(tree);
        tree->roots[i] = ni;
        LodNode* node = &tree->nodes[ni];
        node->depth = 0;
        init_triangle(&node->tri, ico_vert(ICO_FACES[i][0]),
                      ico_vert(ICO_FACES[i][1]), ico_vert(ICO_FACES[i][2]));
        request_mesh(tree, ni);
    }

    return true;
}

void lod_tree_update(LodTree* tree, const double camera_pos_d[3]) {
    // Process completed mesh generation jobs FIRST (main thread, no race)
    process_completed_jobs();

    // Check floating origin recenter
    lod_tree_update_origin(tree, camera_pos_d);

    // Camera position relative to world origin
    tree->camera_pos = HMM_V3(
        (float)(camera_pos_d[0] - tree->world_origin[0]),
        (float)(camera_pos_d[1] - tree->world_origin[1]),
        (float)(camera_pos_d[2] - tree->world_origin[2])
    );

    tree->splits_this_frame = 0;
    tree->uploads_this_frame = 0;

    // Upload any ready meshes first
    for (int i = 0; i < tree->node_count; i++) {
        if (tree->nodes[i].state == LOD_READY && tree->uploads_this_frame < LOD_MAX_UPLOADS) {
            upload_mesh(&tree->nodes[i]);
            tree->uploads_this_frame++;
        }
    }

    // Update LOD tree
    for (int i = 0; i < LOD_ROOT_COUNT; i++) {
        update_node(tree, tree->roots[i]);
    }

    // Collect per-depth stats
    memset(tree->level_stats, 0, sizeof(tree->level_stats));
    for (int i = 0; i < tree->node_count; i++) {
        LodNode* n = &tree->nodes[i];
        if (n->is_leaf && n->gpu_valid && n->depth <= LOD_MAX_DEPTH) {
            tree->level_stats[n->depth].patch_count++;
            tree->level_stats[n->depth].vertex_count += n->gpu_vertex_count;
        }
    }
}

// ---- Rendering (recursive with parent fallback, matches hex-planets) ----

// Per-frame render state
static int g_draw_calls = 0;
static int g_total_verts_drawn = 0;

// Cached FS params (set once per frame, updated per-node for debug depth)
static struct {
    HMM_Vec4 sun_direction;
    HMM_Vec4 camera_position;
    HMM_Vec4 atmosphere_params;
    HMM_Vec4 lod_debug;
} g_fs_params;
static bool g_debug_mode = false;

static void draw_node(const LodNode* node) {
    if (!node->gpu_valid || node->gpu_vertex_count <= 0) return;

    // Per-node: update lod_debug.x with this node's depth
    if (g_debug_mode) {
        g_fs_params.lod_debug.X = (float)node->depth;
        sg_apply_uniforms(1, &SG_RANGE(g_fs_params));
    }

    sg_bindings bind = {0};
    bind.vertex_buffers[0] = node->gpu_buffer;
    sg_apply_bindings(&bind);
    sg_draw(0, node->gpu_vertex_count, 1);
    g_draw_calls++;
    g_total_verts_drawn += node->gpu_vertex_count;
}

// Check if a node can be rendered (has geometry on GPU), recursively
static bool node_can_render(const LodTree* tree, const LodNode* node) {
    // If this node itself has a GPU mesh, it can always render (as fallback or leaf)
    if (node->gpu_valid && node->gpu_vertex_count > 0) return true;
    // If it's a leaf with no mesh, it can't render
    if (node->is_leaf) return false;
    // Internal node without own mesh: check if ALL children can render
    for (int i = 0; i < LOD_CHILDREN; i++) {
        int ci = node->children[i];
        if (ci < 0) return false;
        if (!node_can_render(tree, &tree->nodes[ci])) return false;
    }
    return true;
}

static void render_node_recursive(const LodTree* tree, int node_idx) {
    const LodNode* node = &tree->nodes[node_idx];

    if (node->is_leaf) {
        draw_node(node);
        return;
    }

    // Check if ALL children can render (recursively — no holes)
    bool all_children_renderable = true;
    for (int i = 0; i < LOD_CHILDREN; i++) {
        int ci = node->children[i];
        if (ci < 0 || !node_can_render(tree, &tree->nodes[ci])) {
            all_children_renderable = false;
            break;
        }
    }

    if (all_children_renderable) {
        // All children can render: recurse into them
        for (int i = 0; i < LOD_CHILDREN; i++) {
            if (node->children[i] >= 0)
                render_node_recursive(tree, node->children[i]);
        }
    } else {
        // Children not all ready: render THIS node's mesh as fallback
        // This is the key to zero-blink LOD — parent always covers while children load
        draw_node(node);
    }
}

void lod_tree_render(const LodTree* tree, sg_pipeline pip,
                     HMM_Mat4 view_proj, HMM_Vec4 cam_offset,
                     HMM_Vec4 cam_offset_low, HMM_Vec4 log_depth,
                     HMM_Vec4 sun_dir, HMM_Vec4 cam_pos,
                     HMM_Vec4 atmos_params) {
    sg_apply_pipeline(pip);

    // Apply uniforms once (shared by all nodes)
    struct {
        HMM_Mat4 mvp;
        HMM_Vec4 camera_offset;
        HMM_Vec4 camera_offset_low;
        HMM_Vec4 log_depth_param;
    } vs_params = {
        .mvp = view_proj,
        .camera_offset = cam_offset,
        .camera_offset_low = cam_offset_low,
        .log_depth_param = log_depth,
    };
    sg_apply_uniforms(0, &SG_RANGE(vs_params));

    // Cache FS params for per-node debug updates
    g_fs_params.sun_direction = sun_dir;
    g_fs_params.camera_position = cam_pos;
    g_fs_params.atmosphere_params = atmos_params;
    g_fs_params.lod_debug = HMM_V4(0.0f, (float)tree->max_depth_effective, 0.0f, 0.0f);
    g_debug_mode = tree->show_lod_debug;

    sg_apply_uniforms(1, &SG_RANGE(g_fs_params));

    g_draw_calls = 0;
    g_total_verts_drawn = 0;

    for (int i = 0; i < LOD_ROOT_COUNT; i++) {
        render_node_recursive(tree, tree->roots[i]);
    }

    ((LodTree*)tree)->active_leaf_count = g_draw_calls;
    ((LodTree*)tree)->total_vertex_count = g_total_verts_drawn;
}
