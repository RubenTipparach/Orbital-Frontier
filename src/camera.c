#include "camera.h"
#include <math.h>

void camera_split_double(double val, float* high, float* low) {
    *high = (float)val;
    *low = (float)(val - (double)*high);
}

void camera_get_offset(const Camera* cam, HMM_Vec4* offset_high, HMM_Vec4* offset_low) {
    float hx, lx, hy, ly, hz, lz;
    camera_split_double(cam->pos_d[0], &hx, &lx);
    camera_split_double(cam->pos_d[1], &hy, &ly);
    camera_split_double(cam->pos_d[2], &hz, &lz);
    *offset_high = HMM_V4(hx, hy, hz, 0.0f);
    *offset_low  = HMM_V4(lx, ly, lz, 0.0f);
}

void camera_init(Camera* cam, double start_altitude) {
    *cam = (Camera){0};

    // Start above the planet, looking toward it
    cam->pos_d[0] = 0.0;
    cam->pos_d[1] = start_altitude;
    cam->pos_d[2] = 0.0;

    cam->position = HMM_V3(0.0f, (float)start_altitude, 0.0f);
    cam->local_up = HMM_V3(0.0f, 1.0f, 0.0f);
    // Look toward planet at a slight angle so up/forward aren't antiparallel
    cam->forward = HMM_NormV3(HMM_V3(0.0f, -0.95f, -0.3f));
    cam->up = HMM_V3(0.0f, 0.0f, 1.0f);         // Z-up for this viewing angle
    cam->right = HMM_NormV3(HMM_Cross(cam->forward, cam->up));

    cam->yaw = 0.0f;
    cam->pitch = 0.0f;
    cam->speed = 50000.0f;   // 50 km/s default at orbital scale
    cam->sensitivity = 0.002f;
    cam->mouse_locked = false;
    cam->space_mode = true;
    cam->tangent_initialized = false;
    cam->roll = 0.0f;
    cam->space_up = cam->up;
    cam->space_forward = cam->forward;

    // Ensure orthonormal basis
    cam->right = HMM_NormV3(HMM_Cross(cam->forward, cam->up));
    cam->up = HMM_NormV3(HMM_Cross(cam->right, cam->forward));
}

static void update_space_mode(Camera* cam, float dt) {
    // Apply mouse look
    float dyaw = -cam->mouse_dx_accum * cam->sensitivity;
    float dpitch = -cam->mouse_dy_accum * cam->sensitivity;
    cam->mouse_dx_accum = 0.0f;
    cam->mouse_dy_accum = 0.0f;

    // Roll from Q/E
    float droll = 0.0f;
    if (cam->key_q) droll -= 2.0f * dt;
    if (cam->key_e) droll += 2.0f * dt;

    // Rotate forward around up (yaw)
    HMM_Mat4 yaw_rot = HMM_Rotate_RH(dyaw, cam->space_up);
    cam->space_forward = HMM_NormV3(HMM_MulM4V4(yaw_rot, HMM_V4V(cam->space_forward, 0.0f)).XYZ);

    // Compute right from forward x up
    HMM_Vec3 right = HMM_NormV3(HMM_Cross(cam->space_forward, cam->space_up));

    // Rotate forward around right (pitch)
    HMM_Mat4 pitch_rot = HMM_Rotate_RH(dpitch, right);
    cam->space_forward = HMM_NormV3(HMM_MulM4V4(pitch_rot, HMM_V4V(cam->space_forward, 0.0f)).XYZ);
    cam->space_up = HMM_NormV3(HMM_MulM4V4(pitch_rot, HMM_V4V(cam->space_up, 0.0f)).XYZ);

    // Roll around forward
    if (droll != 0.0f) {
        HMM_Mat4 roll_rot = HMM_Rotate_RH(droll, cam->space_forward);
        cam->space_up = HMM_NormV3(HMM_MulM4V4(roll_rot, HMM_V4V(cam->space_up, 0.0f)).XYZ);
    }

    // Recompute basis
    right = HMM_NormV3(HMM_Cross(cam->space_forward, cam->space_up));
    cam->forward = cam->space_forward;
    cam->right = right;
    cam->up = cam->space_up;

    // Movement
    HMM_Vec3 move = HMM_V3(0, 0, 0);
    if (cam->key_w) move = HMM_AddV3(move, cam->forward);
    if (cam->key_s) move = HMM_SubV3(move, cam->forward);
    if (cam->key_d) move = HMM_AddV3(move, cam->right);
    if (cam->key_a) move = HMM_SubV3(move, cam->right);
    if (cam->key_space) move = HMM_AddV3(move, cam->up);
    if (cam->key_shift) move = HMM_SubV3(move, cam->up);

    float move_len = HMM_LenV3(move);
    if (move_len > 0.001f) {
        move = HMM_MulV3F(move, 1.0f / move_len);
        double dx = (double)(move.X * cam->speed * dt);
        double dy = (double)(move.Y * cam->speed * dt);
        double dz = (double)(move.Z * cam->speed * dt);
        cam->pos_d[0] += dx;
        cam->pos_d[1] += dy;
        cam->pos_d[2] += dz;
    }

    // Sync float position from double
    cam->position = HMM_V3((float)cam->pos_d[0], (float)cam->pos_d[1], (float)cam->pos_d[2]);
}

void camera_update(Camera* cam, float dt, double planet_radius) {
    (void)planet_radius;

    if (!cam->mouse_locked) {
        cam->mouse_dx_accum = 0.0f;
        cam->mouse_dy_accum = 0.0f;
    }

    update_space_mode(cam, dt);

    // Update local_up (radial direction on sphere)
    float pos_len = HMM_LenV3(cam->position);
    if (pos_len > 0.001f) {
        cam->local_up = HMM_MulV3F(cam->position, 1.0f / pos_len);
    }

    // Build view matrix — centered at origin since vertices are camera-relative
    // LookAt expects (eye, target, up) — target must be a point, not a direction
    HMM_Vec3 origin = HMM_V3(0, 0, 0);
    HMM_Vec3 target = cam->forward; // unit vector = point 1m along forward from origin
    cam->view = HMM_LookAt_RH(origin, target, cam->up);

    // Build projection matrix (ZO for D3D11/Metal/WebGPU, NO for OpenGL)
    float aspect = sapp_widthf() / sapp_heightf();
#if defined(SOKOL_GLCORE) || defined(SOKOL_GLES3)
    cam->proj = HMM_Perspective_RH_NO(HMM_AngleDeg(60.0f), aspect, 1.0f, 1e8f);
#else
    cam->proj = HMM_Perspective_RH_ZO(HMM_AngleDeg(60.0f), aspect, 1.0f, 1e8f);
#endif
}

void camera_handle_event(Camera* cam, const sapp_event* ev) {
    if (ev->type == SAPP_EVENTTYPE_MOUSE_DOWN && ev->mouse_button == SAPP_MOUSEBUTTON_LEFT) {
        cam->mouse_locked = true;
        sapp_lock_mouse(true);
    }
    if (ev->type == SAPP_EVENTTYPE_KEY_DOWN && ev->key_code == SAPP_KEYCODE_ESCAPE) {
        cam->mouse_locked = false;
        sapp_lock_mouse(false);
    }
    if (ev->type == SAPP_EVENTTYPE_MOUSE_MOVE && cam->mouse_locked) {
        cam->mouse_dx_accum += ev->mouse_dx;
        cam->mouse_dy_accum += ev->mouse_dy;
    }

    bool pressed = (ev->type == SAPP_EVENTTYPE_KEY_DOWN);
    bool released = (ev->type == SAPP_EVENTTYPE_KEY_UP);
    if (pressed || released) {
        bool state = pressed;
        switch (ev->key_code) {
            case SAPP_KEYCODE_W:      cam->key_w = state; break;
            case SAPP_KEYCODE_S:      cam->key_s = state; break;
            case SAPP_KEYCODE_A:      cam->key_a = state; break;
            case SAPP_KEYCODE_D:      cam->key_d = state; break;
            case SAPP_KEYCODE_SPACE:  cam->key_space = state; break;
            case SAPP_KEYCODE_LEFT_SHIFT: cam->key_shift = state; break;
            case SAPP_KEYCODE_Q:      cam->key_q = state; break;
            case SAPP_KEYCODE_E:      cam->key_e = state; break;
            default: break;
        }
    }

    // Scroll wheel = speed adjustment
    if (ev->type == SAPP_EVENTTYPE_MOUSE_SCROLL) {
        cam->speed *= (ev->scroll_y > 0) ? 1.25f : 0.8f;
        if (cam->speed < 1.0f) cam->speed = 1.0f;
        if (cam->speed > 1e6f) cam->speed = 1e6f;
    }
}
