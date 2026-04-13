#ifndef CAMERA_H
#define CAMERA_H

#include <stdbool.h>
#include "HandmadeMath.h"
#include "sokol_app.h"

typedef struct Camera {
    HMM_Vec3 position;      // float copy for rendering (derived from pos_d each frame)
    double pos_d[3];         // double-precision position accumulator
    float yaw;               // horizontal angle (radians) in local tangent frame
    float pitch;             // vertical angle (radians), clamped
    float speed;             // movement speed (units/sec)
    float sensitivity;       // mouse look sensitivity

    HMM_Vec3 forward;       // look direction (world space)
    HMM_Vec3 right;         // right direction (world space)
    HMM_Vec3 up;            // up direction (world space)
    HMM_Vec3 local_up;      // normalize(position) — radial "up" on sphere
    HMM_Vec3 prev_local_up; // previous frame's local_up (for parallel transport)
    HMM_Vec3 tangent_north; // parallel-transported tangent north
    bool tangent_initialized;

    HMM_Mat4 view;
    HMM_Mat4 proj;
    bool mouse_locked;

    // Movement keys held
    bool key_w, key_s, key_a, key_d;
    bool key_space, key_shift;
    bool key_q, key_e;      // roll keys
    float mouse_dx_accum;
    float mouse_dy_accum;

    // Space flight mode (free-fly, no gravity)
    bool space_mode;
    float roll;
    HMM_Vec3 space_up;
    HMM_Vec3 space_forward;
} Camera;

void camera_init(Camera* cam, double start_altitude);
void camera_update(Camera* cam, float dt, double planet_radius);
void camera_handle_event(Camera* cam, const sapp_event* ev);

// Split a double into float high + float low for GPU upload
void camera_split_double(double val, float* high, float* low);

// Get camera offset (high/low) for camera-relative rendering
void camera_get_offset(const Camera* cam, HMM_Vec4* offset_high, HMM_Vec4* offset_low);

#endif
