#include "render.h"
#include "sokol_glue.h"
#include "util/sokol_debugtext.h"
#include "planet.glsl.h"
#include <math.h>

void renderer_init(Renderer* r, float planet_radius) {
    r->planet_radius = planet_radius;
    r->atmosphere_radius = planet_radius * 1.015f; // 1.5% above surface
    r->sun_angle = 0.0f;

    // Clear to dark space
    r->pass_action = (sg_pass_action){
        .colors[0] = { .load_action = SG_LOADACTION_CLEAR, .clear_value = { 0.01f, 0.01f, 0.02f, 1.0f } },
    };

    // Planet pipeline
    r->planet_pip = sg_make_pipeline(&(sg_pipeline_desc){
        .shader = sg_make_shader(planet_shader_desc(sg_query_backend())),
        .layout = {
            .attrs = {
                [0] = { .format = SG_VERTEXFORMAT_FLOAT3 }, // pos
                [1] = { .format = SG_VERTEXFORMAT_FLOAT3 }, // normal
                [2] = { .format = SG_VERTEXFORMAT_FLOAT3 }, // color
            },
        },
        .index_type = SG_INDEXTYPE_UINT16,
        .cull_mode = SG_CULLMODE_NONE,  // TODO: fix winding order then use BACK
        .depth = {
            .write_enabled = true,
            .compare = SG_COMPAREFUNC_LESS_EQUAL,
        },
        .label = "planet-pipeline",
    });
}

void renderer_frame(Renderer* r, Camera* cam, LodTree* lod, float dt) {
    // Advance sun angle
    r->sun_angle += dt * 0.02f; // slow orbit

    // Compute sun direction
    HMM_Vec3 sun_raw = HMM_V3(cosf(r->sun_angle), 0.3f, sinf(r->sun_angle));
    HMM_Vec3 sun_norm = HMM_NormV3(sun_raw);
    HMM_Vec4 sun_dir = HMM_V4(sun_norm.X, sun_norm.Y, sun_norm.Z, 0.0f);

    // Camera offset for camera-relative rendering
    HMM_Vec4 cam_offset, cam_offset_low;
    camera_get_offset(cam, &cam_offset, &cam_offset_low);

    // Logarithmic depth buffer for planetary scale
    float far_plane = r->planet_radius * 20.0f;
#if defined(SOKOL_GLCORE) || defined(SOKOL_GLES3)
    // GL NDC: z in [-1, 1]. Fcoef maps log2(1+far) to 1, z_bias shifts to [-1,1]
    float Fcoef = 2.0f / log2f(far_plane + 1.0f);
    float z_bias = -1.0f;
#else
    // D3D11/Metal/WebGPU NDC: z in [0, 1]. Fcoef maps log2(1+far) to 1
    float Fcoef = 1.0f / log2f(far_plane + 1.0f);
    float z_bias = 0.0f;
#endif
    HMM_Vec4 log_depth = HMM_V4(Fcoef, far_plane, z_bias, 0.0f);

    // View-projection (camera already provides these)
    HMM_Mat4 vp = HMM_MulM4(cam->proj, cam->view);

    HMM_Vec4 cam_pos = HMM_V4(cam->position.X, cam->position.Y, cam->position.Z, 0.0f);
    HMM_Vec4 atmos_params = HMM_V4(r->planet_radius, r->atmosphere_radius, 8500.0f, 20.0f);

    // Begin pass
    sg_begin_pass(&(sg_pass){ .action = r->pass_action, .swapchain = sglue_swapchain() });

    // Render planet LOD
    lod_tree_render(lod, r->planet_pip, vp, cam_offset, cam_offset_low, log_depth,
                    sun_dir, cam_pos, atmos_params);

    // Debug text
    sdtx_canvas(sapp_widthf() * 0.5f, sapp_heightf() * 0.5f);
    sdtx_origin(1.0f, 1.0f);
    sdtx_color3f(1.0f, 1.0f, 1.0f);

    double alt = sqrt(cam->pos_d[0]*cam->pos_d[0] + cam->pos_d[1]*cam->pos_d[1] + cam->pos_d[2]*cam->pos_d[2]);
    double surface_alt = alt - (double)r->planet_radius;

    // Count active leaves for debug
    int active_leaves = 0;
    for (int i = 0; i < lod->node_count; i++) {
        if (lod->nodes[i].state == LOD_ACTIVE && lod->nodes[i].children[0] < 0)
            active_leaves++;
    }

    sdtx_printf("Orbital Frontier\n");
    sdtx_printf("Alt: %.1f km  Speed: %.0f km/s\n", surface_alt / 1000.0, (double)cam->speed / 1000.0);
    sdtx_printf("Nodes: %d  Active: %d  FPS: %.0f\n", lod->node_count, active_leaves, 1.0f / (dt > 0.0001f ? dt : 0.016f));
    sdtx_draw();

    sg_end_pass();
    sg_commit();
}

void renderer_shutdown(Renderer* r) {
    sg_destroy_pipeline(r->planet_pip);
}
