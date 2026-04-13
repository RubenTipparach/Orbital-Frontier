#include "sokol_app.h"
#include "sokol_gfx.h"
#include "sokol_glue.h"
#include "sokol_log.h"
#include "sokol_time.h"
#include "util/sokol_debugtext.h"

#define HANDMADE_MATH_IMPLEMENTATION
#include "HandmadeMath.h"

#include "camera.h"
#include "render.h"
#include "lod.h"

#include <stdio.h>
#include <math.h>

#define PLANET_RADIUS 800000.0f  // 800km
#define TERRAIN_SEED  42
#define START_ALTITUDE_FACTOR 3.0  // start at 3x planet radius

static struct {
    Camera camera;
    Renderer renderer;
    LodTree lod;
    uint64_t last_time;
    bool initialized;
} state;

static void init(void) {
    // Init sokol_gfx with large buffer pool for LOD
    sg_setup(&(sg_desc){
        .environment = sglue_environment(),
        .logger.func = slog_func,
        .buffer_pool_size = 4096,
        .pipeline_pool_size = 32,
        .shader_pool_size = 32,
    });

    // Init sokol_time
    stm_setup();
    state.last_time = stm_now();

    // Init debug text
    sdtx_setup(&(sdtx_desc_t){
        .fonts[0] = sdtx_font_cpc(),
        .logger.func = slog_func,
    });

    // Init camera at 3x planet radius altitude
    double start_alt = (double)PLANET_RADIUS * START_ALTITUDE_FACTOR;
    camera_init(&state.camera, start_alt);

    // Init LOD tree
    lod_tree_init(&state.lod, PLANET_RADIUS, TERRAIN_SEED);

    // Init renderer
    renderer_init(&state.renderer, PLANET_RADIUS);

    state.initialized = true;
}

static void frame(void) {
    if (!state.initialized) return;

    // Compute delta time
    uint64_t now = stm_now();
    float dt = (float)stm_sec(stm_diff(now, state.last_time));
    state.last_time = now;
    if (dt > 0.1f) dt = 0.1f; // cap at 100ms

    // Update camera
    camera_update(&state.camera, dt, (double)PLANET_RADIUS);

    // Update LOD tree
    lod_tree_update(&state.lod, state.camera.pos_d);

    // Render
    renderer_frame(&state.renderer, &state.camera, &state.lod, dt);
}

static void event(const sapp_event* ev) {
    camera_handle_event(&state.camera, ev);
}

static void cleanup(void) {
    lod_tree_destroy(&state.lod);
    renderer_shutdown(&state.renderer);
    sdtx_shutdown();
    sg_shutdown();
}

sapp_desc sokol_main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    return (sapp_desc){
        .init_cb = init,
        .frame_cb = frame,
        .event_cb = event,
        .cleanup_cb = cleanup,
        .width = 1280,
        .height = 720,
        .window_title = "Orbital Frontier",
        .icon.sokol_default = true,
        .logger.func = slog_func,
    };
}
