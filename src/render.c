#include "render.h"
#include "sokol_glue.h"
#include "util/sokol_debugtext.h"
#include "planet.glsl.h"
#include <math.h>

void renderer_init(Renderer* r, float planet_radius) {
    r->planet_radius = planet_radius;
    r->atmosphere_radius = planet_radius + 12000.0f;
    r->sun_angle = 0.0f;

    r->pass_action = (sg_pass_action){
        .colors[0] = { .load_action = SG_LOADACTION_CLEAR, .clear_value = { 0.01f, 0.01f, 0.02f, 1.0f } },
    };

    // Planet pipeline — matches hex-planets: CCW winding, back-face cull
    r->planet_pip = sg_make_pipeline(&(sg_pipeline_desc){
        .shader = sg_make_shader(planet_shader_desc(sg_query_backend())),
        .layout = {
            .attrs = {
                [0] = { .format = SG_VERTEXFORMAT_FLOAT3 }, // pos
                [1] = { .format = SG_VERTEXFORMAT_FLOAT3 }, // normal
                [2] = { .format = SG_VERTEXFORMAT_FLOAT3 }, // color
            },
        },
        .depth = {
            .compare = SG_COMPAREFUNC_LESS_EQUAL,
            .write_enabled = true,
        },
        .cull_mode = SG_CULLMODE_BACK,
        .face_winding = SG_FACEWINDING_CCW,
        .label = "planet-pipeline",
    });
}

void renderer_frame(Renderer* r, Camera* cam, LodTree* lod, float dt) {
    r->sun_angle += dt * 0.02f;

    HMM_Vec3 sun_norm = HMM_NormV3(HMM_V3(cosf(r->sun_angle), 0.3f, sinf(r->sun_angle)));
    HMM_Vec4 sun_dir = HMM_V4(sun_norm.X, sun_norm.Y, sun_norm.Z, 0.0f);

    // Camera offset: camera position relative to floating origin, split high+low
    double cam_rel[3] = {
        cam->pos_d[0] - lod->world_origin[0],
        cam->pos_d[1] - lod->world_origin[1],
        cam->pos_d[2] - lod->world_origin[2],
    };
    float hi_x = (float)cam_rel[0], lo_x = (float)(cam_rel[0] - (double)hi_x);
    float hi_y = (float)cam_rel[1], lo_y = (float)(cam_rel[1] - (double)hi_y);
    float hi_z = (float)cam_rel[2], lo_z = (float)(cam_rel[2] - (double)hi_z);
    HMM_Vec4 cam_offset = HMM_V4(hi_x, hi_y, hi_z, 0.0f);
    HMM_Vec4 cam_offset_low = HMM_V4(lo_x, lo_y, lo_z, 0.0f);

    // Log depth (matches hex-planets: 10,000 km far plane)
    float far_plane = 10000000.0f;
    float Fcoef, z_bias;
    sg_backend backend = sg_query_backend();
    if (backend == SG_BACKEND_GLCORE || backend == SG_BACKEND_GLES3) {
        Fcoef = 2.0f / log2f(far_plane + 1.0f);
        z_bias = -1.0f;
    } else {
        Fcoef = 1.0f / log2f(far_plane + 1.0f);
        z_bias = 0.0f;
    }
    HMM_Vec4 log_depth = HMM_V4(Fcoef, far_plane, z_bias, 0.0f);

    // Rotation-only view-projection (matches hex-planets: zero translation row)
    HMM_Mat4 view_rot = cam->view;
    view_rot.Elements[3][0] = 0.0f;
    view_rot.Elements[3][1] = 0.0f;
    view_rot.Elements[3][2] = 0.0f;
    view_rot.Elements[3][3] = 1.0f;
    HMM_Mat4 vp = HMM_MulM4(cam->proj, view_rot);

    // Camera world position (for fragment shader lighting)
    HMM_Vec4 cam_pos = HMM_V4((float)cam->pos_d[0], (float)cam->pos_d[1], (float)cam->pos_d[2], 0.0f);
    HMM_Vec4 atmos_params = HMM_V4(r->planet_radius, r->atmosphere_radius, 0.0f, 0.0f);

    // Render
    sg_begin_pass(&(sg_pass){ .action = r->pass_action, .swapchain = sglue_swapchain() });

    lod_tree_render(lod, r->planet_pip, vp, cam_offset, cam_offset_low, log_depth,
                    sun_dir, cam_pos, atmos_params);

    // HUD
    sdtx_canvas(sapp_widthf() * 0.5f, sapp_heightf() * 0.5f);
    sdtx_origin(1.0f, 1.0f);
    sdtx_color3f(1.0f, 1.0f, 1.0f);

    double alt = sqrt(cam->pos_d[0]*cam->pos_d[0] + cam->pos_d[1]*cam->pos_d[1] + cam->pos_d[2]*cam->pos_d[2]);
    double surface_alt = alt - (double)r->planet_radius;

    int active_leaves = 0;
    for (int i = 0; i < lod->node_count; i++) {
        if (lod->nodes[i].gpu_valid && lod->nodes[i].is_leaf)
            active_leaves++;
    }

    sdtx_printf("Orbital Frontier\n");
    sdtx_printf("Alt: %.1f km  Speed: %.0f km/s\n", surface_alt / 1000.0, (double)cam->speed / 1000.0);
    sdtx_printf("Draws: %d  Verts: %dk  Nodes: %d/%d  FPS: %.0f\n",
                lod->active_leaf_count, lod->total_vertex_count / 1000,
                lod->node_count, LOD_MAX_NODES,
                1.0f / (dt > 0.0001f ? dt : 0.016f));
    // Per-depth stats (compact)
    sdtx_printf("LOD:");
    for (int d = 0; d <= LOD_MAX_DEPTH; d++) {
        if (lod->level_stats[d].patch_count > 0)
            sdtx_printf(" d%d:%d", d, lod->level_stats[d].patch_count);
    }
    sdtx_printf("\n");
    if (lod->show_lod_debug) sdtx_printf("[L] Debug colors ON\n");
    sdtx_draw();

    sg_end_pass();
    sg_commit();
}

void renderer_shutdown(Renderer* r) {
    sg_destroy_pipeline(r->planet_pip);
}
