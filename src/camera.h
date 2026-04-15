#ifndef CAMERA_H
#define CAMERA_H

#include <stdbool.h>
#include "HandmadeMath.h"
#include "sokol_app.h"
#include "terrain_noise.h"

typedef struct Camera {
    HMM_Vec3 position;      // float copy for rendering (derived from pos_d each frame)
    double pos_d[3];         // double-precision position accumulator
    float yaw;
    float pitch;
    float speed;
    float sensitivity;

    HMM_Vec3 forward;
    HMM_Vec3 right;
    HMM_Vec3 up;
    HMM_Vec3 local_up;      // normalize(position) — radial "up" on sphere

    HMM_Mat4 view;           // full view matrix (with translation)
    HMM_Mat4 proj;
    bool mouse_locked;

    bool key_w, key_s, key_a, key_d;
    bool key_space, key_shift;
    bool key_q, key_e;
    float mouse_dx_accum;
    float mouse_dy_accum;

    bool space_mode;
    float roll;
    HMM_Vec3 space_up;
    HMM_Vec3 space_forward;

    // Terrain collision
    TerrainNoise* terrain;
    float planet_radius;
    float altitude;          // current height above terrain (meters)
    float min_altitude;      // minimum clearance above terrain (meters)
} Camera;

void camera_init(Camera* cam, double start_altitude, float planet_radius, TerrainNoise* terrain);
void camera_update(Camera* cam, float dt, double planet_radius);
void camera_handle_event(Camera* cam, const sapp_event* ev);

#endif
