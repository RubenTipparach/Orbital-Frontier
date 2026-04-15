#include "sokol_app.h"
#include "sokol_gfx.h"
#include "sokol_glue.h"
#include "sokol_log.h"
#include "sokol_time.h"
#include "util/sokol_debugtext.h"

#define HANDMADE_MATH_IMPLEMENTATION
#include "HandmadeMath.h"

#include "debug_log.h"
#include "camera.h"
#include "render.h"
#include "lod.h"
#include "screenshot.h"

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
    int frame_count;
} state;

static void init(void) {
    debug_log_init();
    debug_log("Initializing Orbital Frontier...");

    // Init sokol_gfx with large buffer pool for LOD
    debug_log("Setting up sokol_gfx (buffer_pool=16384)");
    sg_setup(&(sg_desc){
        .environment = sglue_environment(),
        .logger = { .func = debug_sokol_logger },
        .buffer_pool_size = 16384,
        .pipeline_pool_size = 32,
        .shader_pool_size = 32,
        // validation enabled — catches bad bindings early
    });

    sg_backend backend = sg_query_backend();
    const char* backend_names[] = {
        "GLCORE", "GLES3", "D3D11", "METAL_IOS", "METAL_MACOS",
        "METAL_SIMULATOR", "WGPU", "DUMMY"
    };
    debug_log("Backend: %s", (backend < 8) ? backend_names[backend] : "UNKNOWN");

    // Init sokol_time
    stm_setup();
    state.last_time = stm_now();

    // Init debug text
    debug_log("Setting up debug text");
    sdtx_setup(&(sdtx_desc_t){
        .fonts[0] = sdtx_font_cpc(),
        .logger = { .func = debug_sokol_logger },
    });

    // Init LOD tree (before camera, so terrain is available for collision)
    debug_log("Initializing LOD tree (radius=%.0f, seed=%d)", (double)PLANET_RADIUS, TERRAIN_SEED);
    lod_tree_init(&state.lod, PLANET_RADIUS, TERRAIN_SEED);
    debug_log("LOD tree created: %d root nodes", LOD_ROOT_COUNT);

    // Init camera with terrain collision
    double start_alt = (double)PLANET_RADIUS * START_ALTITUDE_FACTOR;
    camera_init(&state.camera, start_alt, PLANET_RADIUS, &state.lod.terrain);
    debug_log("Camera initialized at altitude %.0f km", (start_alt - PLANET_RADIUS) / 1000.0);
    debug_log("  pos: (%.0f, %.0f, %.0f)", state.camera.pos_d[0], state.camera.pos_d[1], state.camera.pos_d[2]);
    debug_log("  forward: (%.3f, %.3f, %.3f)", (double)state.camera.forward.X, (double)state.camera.forward.Y, (double)state.camera.forward.Z);
    debug_log("  up: (%.3f, %.3f, %.3f)", (double)state.camera.up.X, (double)state.camera.up.Y, (double)state.camera.up.Z);
    debug_log("  right: (%.3f, %.3f, %.3f)", (double)state.camera.right.X, (double)state.camera.right.Y, (double)state.camera.right.Z);

    // Init renderer
    debug_log("Initializing renderer");
    renderer_init(&state.renderer, PLANET_RADIUS);

    state.initialized = true;
    state.frame_count = 0;
    debug_log("Initialization complete. Window: %dx%d", sapp_width(), sapp_height());
}

static void frame(void) {
    if (!state.initialized) return;

    // Compute delta time
    uint64_t now = stm_now();
    float dt = (float)stm_sec(stm_diff(now, state.last_time));
    state.last_time = now;
    if (dt > 0.1f) dt = 0.1f; // cap at 100ms

    state.frame_count++;

    // Update camera
    camera_update(&state.camera, dt, (double)PLANET_RADIUS);

    // Update LOD tree
    lod_tree_update(&state.lod, state.camera.pos_d);

    // Render
    renderer_frame(&state.renderer, &state.camera, &state.lod, dt);

    // Periodic logging (every 2 seconds ~120 frames)
    if (state.frame_count == 1 || state.frame_count % 120 == 0) {
        int active = 0, generating = 0, ready = 0, unloaded = 0;
        for (int i = 0; i < state.lod.node_count; i++) {
            LodNode* n = &state.lod.nodes[i];
            bool leaf = (n->children[0] < 0);
            if (!leaf) continue;
            switch (n->state) {
                case LOD_ACTIVE: active++; break;
                case LOD_GENERATING: generating++; break;
                case LOD_READY: ready++; break;
                case LOD_UNLOADED: unloaded++; break;
            }
        }
        double alt = sqrt(state.camera.pos_d[0]*state.camera.pos_d[0] +
                          state.camera.pos_d[1]*state.camera.pos_d[1] +
                          state.camera.pos_d[2]*state.camera.pos_d[2]);
        debug_log("Frame %d (%.1f fps): leaves active=%d gen=%d ready=%d unloaded=%d | total_nodes=%d | alt=%.0f km",
                  state.frame_count, 1.0f / (dt > 0.0001f ? dt : 0.016f),
                  active, generating, ready, unloaded,
                  state.lod.node_count, (alt - PLANET_RADIUS) / 1000.0);

        if (state.frame_count == 1) {
            debug_log("  cam_pos: (%.0f, %.0f, %.0f)",
                      state.camera.pos_d[0], state.camera.pos_d[1], state.camera.pos_d[2]);
            // Log a few node positions
            for (int i = 0; i < 3 && i < state.lod.node_count; i++) {
                LodNode* n = &state.lod.nodes[i];
                debug_log("  node[%d] depth=%d state=%d center=(%.3f,%.3f,%.3f) ar=%.4f leaf=%d ch=(%d,%d,%d,%d)",
                          i, n->depth, n->state,
                          (double)n->tri.center.X, (double)n->tri.center.Y, (double)n->tri.center.Z,
                          (double)n->tri.angular_radius, n->is_leaf,
                          n->children[0], n->children[1], n->children[2], n->children[3]);
            }
        }
    }
}

static void event(const sapp_event* ev) {
    camera_handle_event(&state.camera, ev);

    // Log key events for debugging
    if (ev->type == SAPP_EVENTTYPE_KEY_DOWN) {
        if (ev->key_code == SAPP_KEYCODE_F5 && (ev->modifiers & SAPP_MODIFIER_CTRL)) {
            screenshot_capture();
        }
        if (ev->key_code == SAPP_KEYCODE_F1) {
            debug_log("--- F1 Debug Dump ---");
            debug_log("Camera pos: (%.2f, %.2f, %.2f)",
                      state.camera.pos_d[0], state.camera.pos_d[1], state.camera.pos_d[2]);
            debug_log("Camera fwd: (%.3f, %.3f, %.3f)",
                      (double)state.camera.forward.X, (double)state.camera.forward.Y, (double)state.camera.forward.Z);
            debug_log("Camera up: (%.3f, %.3f, %.3f)",
                      (double)state.camera.up.X, (double)state.camera.up.Y, (double)state.camera.up.Z);
            debug_log("Speed: %.0f m/s", (double)state.camera.speed);
            debug_log("LOD nodes: %d", state.lod.node_count);

            // View matrix dump
            HMM_Mat4 v = state.camera.view;
            debug_log("View matrix:");
            debug_log("  [%.4f %.4f %.4f %.4f]", (double)v.Elements[0][0], (double)v.Elements[0][1], (double)v.Elements[0][2], (double)v.Elements[0][3]);
            debug_log("  [%.4f %.4f %.4f %.4f]", (double)v.Elements[1][0], (double)v.Elements[1][1], (double)v.Elements[1][2], (double)v.Elements[1][3]);
            debug_log("  [%.4f %.4f %.4f %.4f]", (double)v.Elements[2][0], (double)v.Elements[2][1], (double)v.Elements[2][2], (double)v.Elements[2][3]);
            debug_log("  [%.4f %.4f %.4f %.4f]", (double)v.Elements[3][0], (double)v.Elements[3][1], (double)v.Elements[3][2], (double)v.Elements[3][3]);

            // Per-depth stats
            for (int d = 0; d <= LOD_MAX_DEPTH; d++) {
                if (state.lod.level_stats[d].patch_count > 0) {
                    debug_log("  depth %d: %d patches, %dk verts",
                              d, state.lod.level_stats[d].patch_count,
                              state.lod.level_stats[d].vertex_count / 1000);
                }
            }
        }
        if (ev->key_code == SAPP_KEYCODE_L) {
            state.lod.show_lod_debug = !state.lod.show_lod_debug;
            debug_log("LOD debug: %s", state.lod.show_lod_debug ? "ON" : "OFF");
        }
    }
}

static void cleanup(void) {
    debug_log("Cleaning up...");
    lod_tree_destroy(&state.lod);
    renderer_shutdown(&state.renderer);
    sdtx_shutdown();
    sg_shutdown();
    debug_log("Cleanup complete");
    debug_log_shutdown();
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
        .logger = { .func = debug_sokol_logger },
    };
}
