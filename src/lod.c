#include "lod.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>

// Icosahedron base vertices
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

// 20 triangular faces of the icosahedron
static const int ICO_FACES[20][3] = {
    {0,2,1}, {0,3,2}, {0,4,3}, {0,5,4}, {0,1,5},
    {1,2,6}, {2,3,7}, {3,4,8}, {4,5,9}, {5,1,10},
    {6,2,7}, {7,3,8}, {8,4,9}, {9,5,10}, {10,1,6},
    {11,6,7}, {11,7,8}, {11,8,9}, {11,9,10}, {11,10,6},
};

static HMM_Vec3 ico_vert(int i) {
    return HMM_NormV3(HMM_V3(ICO_VERTS[i][0], ICO_VERTS[i][1], ICO_VERTS[i][2]));
}

static int alloc_node(LodTree* tree) {
    if (tree->node_count >= LOD_MAX_NODES) return -1;
    int idx = tree->node_count++;
    memset(&tree->nodes[idx], 0, sizeof(LodNode));
    tree->nodes[idx].parent = -1;
    for (int i = 0; i < LOD_CHILDREN; i++) tree->nodes[idx].children[i] = -1;
    return idx;
}

static void free_node_gpu(LodNode* node) {
    if (node->gpu_valid) {
        sg_destroy_buffer(node->vbuf);
        sg_destroy_buffer(node->ibuf);
        node->gpu_valid = false;
    }
    free(node->vertices);
    node->vertices = NULL;
    free(node->indices);
    node->indices = NULL;
    node->vertex_count = 0;
    node->index_count = 0;
    node->state = LOD_UNLOADED;
}

// Mesh generation job data (copied by value for thread safety)
typedef struct {
    int node_idx;
    LodTree* tree;
    HMM_Vec3 v0, v1, v2;
    float planet_radius;
    double world_origin[3];
    TerrainNoise terrain;
} MeshGenJob;

static HMM_Vec3 slerp_on_sphere(HMM_Vec3 a, HMM_Vec3 b, float t) {
    float dot = HMM_DotV3(a, b);
    if (dot > 0.9999f) {
        // Nearly identical — lerp and normalize
        HMM_Vec3 r = HMM_V3(a.X + (b.X - a.X) * t, a.Y + (b.Y - a.Y) * t, a.Z + (b.Z - a.Z) * t);
        return HMM_NormV3(r);
    }
    float theta = acosf(dot < -1.0f ? -1.0f : (dot > 1.0f ? 1.0f : dot));
    float sin_theta = sinf(theta);
    float wa = sinf((1.0f - t) * theta) / sin_theta;
    float wb = sinf(t * theta) / sin_theta;
    return HMM_NormV3(HMM_V3(a.X * wa + b.X * wb, a.Y * wa + b.Y * wb, a.Z * wa + b.Z * wb));
}

static void generate_mesh(void* data) {
    MeshGenJob* job = (MeshGenJob*)data;
    LodTree* tree = job->tree;
    LodNode* node = &tree->nodes[job->node_idx];

    int n = LOD_VERTS_PER_EDGE;
    int vert_count = (n * (n + 1)) / 2;
    int tri_count = (n - 1) * (n - 1);
    int idx_count = tri_count * 3;

    LodVertex* verts = (LodVertex*)calloc(vert_count, sizeof(LodVertex));
    uint16_t* indices = (uint16_t*)calloc(idx_count, sizeof(uint16_t));

    // Generate vertices on the triangle (barycentric interpolation on sphere)
    int vi = 0;
    for (int row = 0; row < n; row++) {
        float v = (float)row / (float)(n - 1);
        HMM_Vec3 left = slerp_on_sphere(job->v0, job->v2, v);
        HMM_Vec3 right = slerp_on_sphere(job->v1, job->v2, v);
        int cols = n - row;
        for (int col = 0; col < cols; col++) {
            float u = (cols > 1) ? (float)col / (float)(cols - 1) : 0.0f;
            HMM_Vec3 unit_pos = slerp_on_sphere(left, right, u);

            // Sample terrain height
            float height_m = terrain_sample_height_m(&job->terrain, unit_pos.X, unit_pos.Y, unit_pos.Z);
            float radius = job->planet_radius + height_m;

            // World position (relative to floating origin)
            double wx = (double)unit_pos.X * (double)radius - job->world_origin[0];
            double wy = (double)unit_pos.Y * (double)radius - job->world_origin[1];
            double wz = (double)unit_pos.Z * (double)radius - job->world_origin[2];

            verts[vi].pos[0] = (float)wx;
            verts[vi].pos[1] = (float)wy;
            verts[vi].pos[2] = (float)wz;

            // Normal = unit sphere direction (approximate, good enough for planet scale)
            verts[vi].normal[0] = unit_pos.X;
            verts[vi].normal[1] = unit_pos.Y;
            verts[vi].normal[2] = unit_pos.Z;

            // Compute slope from finite differences
            float eps = 0.001f;
            HMM_Vec3 dx_dir = HMM_NormV3(HMM_V3(unit_pos.X + eps, unit_pos.Y, unit_pos.Z));
            HMM_Vec3 dy_dir = HMM_NormV3(HMM_V3(unit_pos.X, unit_pos.Y + eps, unit_pos.Z));
            float hx = terrain_sample_height_m(&job->terrain, dx_dir.X, dx_dir.Y, dx_dir.Z);
            float hy = terrain_sample_height_m(&job->terrain, dy_dir.X, dy_dir.Y, dy_dir.Z);
            float slope = sqrtf((hx - height_m) * (hx - height_m) + (hy - height_m) * (hy - height_m)) / (eps * job->planet_radius);

            // Biome color
            HMM_Vec3 color = terrain_biome_color(height_m, slope);
            verts[vi].color[0] = color.X;
            verts[vi].color[1] = color.Y;
            verts[vi].color[2] = color.Z;

            vi++;
        }
    }

    // Generate triangle indices
    int ii = 0;
    int row_start = 0;
    for (int row = 0; row < n - 1; row++) {
        int cols = n - row;
        int next_row_start = row_start + cols;
        for (int col = 0; col < cols - 1; col++) {
            // Upward triangle
            indices[ii++] = (uint16_t)(row_start + col);
            indices[ii++] = (uint16_t)(row_start + col + 1);
            indices[ii++] = (uint16_t)(next_row_start + col);

            // Downward triangle (if not last column in next row)
            if (col < cols - 2) {
                indices[ii++] = (uint16_t)(row_start + col + 1);
                indices[ii++] = (uint16_t)(next_row_start + col + 1);
                indices[ii++] = (uint16_t)(next_row_start + col);
            }
        }
        row_start = next_row_start;
    }

    node->vertices = verts;
    node->vertex_count = vert_count;
    node->indices = indices;
    node->index_count = ii;
    node->state = LOD_READY;

    free(job);
}

static void request_mesh(LodTree* tree, int node_idx) {
    LodNode* node = &tree->nodes[node_idx];
    if (node->state != LOD_UNLOADED) return;

    MeshGenJob* job = (MeshGenJob*)calloc(1, sizeof(MeshGenJob));
    job->node_idx = node_idx;
    job->tree = tree;
    job->v0 = node->v0;
    job->v1 = node->v1;
    job->v2 = node->v2;
    job->planet_radius = tree->planet_radius;
    memcpy(job->world_origin, tree->world_origin, sizeof(double) * 3);
    job->terrain = tree->terrain;

    node->state = LOD_GENERATING;
    job_system_submit(tree->jobs, generate_mesh, job);
}

static void upload_mesh(LodNode* node) {
    if (node->state != LOD_READY || !node->vertices) return;

    node->vbuf = sg_make_buffer(&(sg_buffer_desc){
        .data = { .ptr = node->vertices, .size = node->vertex_count * sizeof(LodVertex) },
        .label = "lod-vbuf",
    });
    node->ibuf = sg_make_buffer(&(sg_buffer_desc){
        .usage.index_buffer = true,
        .data = { .ptr = node->indices, .size = node->index_count * sizeof(uint16_t) },
        .label = "lod-ibuf",
    });
    node->gpu_valid = true;
    node->state = LOD_ACTIVE;

    // Free CPU copies
    free(node->vertices);
    node->vertices = NULL;
    free(node->indices);
    node->indices = NULL;
}

// Aperture-4 subdivision: split triangle into 4 children
static void split_node(LodTree* tree, int node_idx) {
    LodNode* node = &tree->nodes[node_idx];
    if (node->children[0] >= 0) return; // already split
    if (node->depth >= LOD_MAX_DEPTH) return;

    HMM_Vec3 m01 = HMM_NormV3(HMM_MulV3F(HMM_AddV3(node->v0, node->v1), 0.5f));
    HMM_Vec3 m12 = HMM_NormV3(HMM_MulV3F(HMM_AddV3(node->v1, node->v2), 0.5f));
    HMM_Vec3 m02 = HMM_NormV3(HMM_MulV3F(HMM_AddV3(node->v0, node->v2), 0.5f));

    HMM_Vec3 child_tris[4][3] = {
        { node->v0, m01, m02 },
        { m01, node->v1, m12 },
        { m02, m12, node->v2 },
        { m01, m12, m02 },     // center triangle
    };

    for (int i = 0; i < LOD_CHILDREN; i++) {
        int ci = alloc_node(tree);
        if (ci < 0) return;
        node = &tree->nodes[node_idx]; // re-fetch after potential realloc

        LodNode* child = &tree->nodes[ci];
        child->parent = node_idx;
        child->depth = node->depth + 1;
        child->v0 = child_tris[i][0];
        child->v1 = child_tris[i][1];
        child->v2 = child_tris[i][2];
        child->center = HMM_NormV3(HMM_MulV3F(HMM_AddV3(HMM_AddV3(child->v0, child->v1), child->v2), 1.0f / 3.0f));
        child->arc = acosf(HMM_DotV3(child->v0, child->v1));

        node->children[i] = ci;
    }
}

static void merge_node(LodTree* tree, int node_idx) {
    LodNode* node = &tree->nodes[node_idx];
    for (int i = 0; i < LOD_CHILDREN; i++) {
        if (node->children[i] >= 0) {
            merge_node(tree, node->children[i]);
            free_node_gpu(&tree->nodes[node->children[i]]);
            node->children[i] = -1;
        }
    }
}

static bool is_leaf(const LodNode* node) {
    return node->children[0] < 0;
}

static float node_distance(const LodNode* node, const float cam_pos[3], float planet_radius) {
    // Distance from camera to patch center on sphere surface
    float cx = node->center.X * planet_radius;
    float cy = node->center.Y * planet_radius;
    float cz = node->center.Z * planet_radius;
    float dx = cx - cam_pos[0];
    float dy = cy - cam_pos[1];
    float dz = cz - cam_pos[2];
    return sqrtf(dx * dx + dy * dy + dz * dz);
}

static void update_node(LodTree* tree, int node_idx, const float cam_pos[3]) {
    LodNode* node = &tree->nodes[node_idx];
    float dist = node_distance(node, cam_pos, tree->planet_radius);
    float threshold = node->arc * tree->split_factor * tree->planet_radius;

    if (is_leaf(node)) {
        // Should we split?
        if (dist < threshold && node->depth < LOD_MAX_DEPTH && tree->splits_this_frame < LOD_MAX_SPLITS) {
            split_node(tree, node_idx);
            tree->splits_this_frame++;
            // Request mesh for children
            node = &tree->nodes[node_idx]; // re-fetch
            for (int i = 0; i < LOD_CHILDREN; i++) {
                if (node->children[i] >= 0) {
                    request_mesh(tree, node->children[i]);
                }
            }
        } else {
            // Ensure this leaf has a mesh
            if (node->state == LOD_UNLOADED) {
                request_mesh(tree, node_idx);
            }
            if (node->state == LOD_READY && tree->uploads_this_frame < LOD_MAX_UPLOADS) {
                upload_mesh(node);
                tree->uploads_this_frame++;
            }
        }
    } else {
        // Should we merge?
        if (dist > threshold * 1.5f) {
            merge_node(tree, node_idx);
            // Re-request mesh for this node as a leaf
            node = &tree->nodes[node_idx];
            if (node->state == LOD_UNLOADED) {
                request_mesh(tree, node_idx);
            }
        } else {
            // Recurse into children
            for (int i = 0; i < LOD_CHILDREN; i++) {
                if (node->children[i] >= 0) {
                    update_node(tree, node->children[i], cam_pos);
                }
            }
        }
    }
}

void lod_tree_init(LodTree* tree, float planet_radius, int seed) {
    memset(tree, 0, sizeof(LodTree));
    tree->planet_radius = planet_radius;
    tree->split_factor = LOD_SPLIT_FACTOR;

    // Precompute arc per depth
    float base_arc = acosf(HMM_DotV3(ico_vert(ICO_FACES[0][0]), ico_vert(ICO_FACES[0][1])));
    for (int d = 0; d <= LOD_MAX_DEPTH; d++) {
        tree->depth_arc[d] = base_arc / powf(2.0f, (float)d);
    }

    // Init terrain
    terrain_noise_init(&tree->terrain, seed);

    // Create job system
    tree->jobs = job_system_create(LOD_NUM_WORKERS);

    // Create root nodes from icosahedron faces
    for (int i = 0; i < LOD_ROOT_COUNT; i++) {
        int ni = alloc_node(tree);
        tree->roots[i] = ni;

        LodNode* node = &tree->nodes[ni];
        node->depth = 0;
        node->v0 = ico_vert(ICO_FACES[i][0]);
        node->v1 = ico_vert(ICO_FACES[i][1]);
        node->v2 = ico_vert(ICO_FACES[i][2]);
        node->center = HMM_NormV3(HMM_MulV3F(HMM_AddV3(HMM_AddV3(node->v0, node->v1), node->v2), 1.0f / 3.0f));
        node->arc = base_arc;
    }
}

void lod_tree_destroy(LodTree* tree) {
    // Wait for pending jobs
    job_system_flush(tree->jobs);

    for (int i = 0; i < tree->node_count; i++) {
        free_node_gpu(&tree->nodes[i]);
    }
    job_system_destroy(tree->jobs);
}

void lod_tree_update(LodTree* tree, const double camera_pos_d[3]) {
    // Camera position relative to world origin (float for LOD distance checks)
    float cam_pos[3] = {
        (float)(camera_pos_d[0] - tree->world_origin[0]),
        (float)(camera_pos_d[1] - tree->world_origin[1]),
        (float)(camera_pos_d[2] - tree->world_origin[2]),
    };

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
        update_node(tree, tree->roots[i], cam_pos);
    }
}

void lod_tree_render(const LodTree* tree, sg_pipeline pip,
                     HMM_Mat4 view_proj, HMM_Vec4 cam_offset,
                     HMM_Vec4 cam_offset_low, HMM_Vec4 log_depth,
                     HMM_Vec4 sun_dir, HMM_Vec4 cam_pos,
                     HMM_Vec4 atmos_params) {
    sg_apply_pipeline(pip);

    for (int i = 0; i < tree->node_count; i++) {
        const LodNode* node = &tree->nodes[i];
        if (node->state != LOD_ACTIVE || !node->gpu_valid) continue;
        if (!is_leaf(node)) continue; // only render leaves

        sg_apply_bindings(&(sg_bindings){
            .vertex_buffers[0] = node->vbuf,
            .index_buffer = node->ibuf,
        });

        // VS uniforms
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

        // FS uniforms
        struct {
            HMM_Vec4 sun_direction;
            HMM_Vec4 camera_position;
            HMM_Vec4 atmosphere_params;
        } fs_params = {
            .sun_direction = sun_dir,
            .camera_position = cam_pos,
            .atmosphere_params = atmos_params,
        };
        sg_apply_uniforms(1, &SG_RANGE(fs_params));

        sg_draw(0, node->index_count, 1);
    }
}
