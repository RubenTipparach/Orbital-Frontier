#include "camera.h"
#include <math.h>

void camera_init(Camera* cam, double start_altitude, float planet_radius, TerrainNoise* terrain) {
    *cam = (Camera){0};

    cam->pos_d[0] = 0.0;
    cam->pos_d[1] = start_altitude;
    cam->pos_d[2] = 0.0;

    cam->position = HMM_V3(0.0f, (float)start_altitude, 0.0f);
    cam->local_up = HMM_V3(0.0f, 1.0f, 0.0f);

    // Look toward planet at a slight angle
    cam->forward = HMM_NormV3(HMM_V3(0.0f, -0.95f, -0.3f));
    cam->up = HMM_V3(0.0f, 0.0f, 1.0f);
    cam->right = HMM_NormV3(HMM_Cross(cam->forward, cam->up));
    cam->up = HMM_NormV3(HMM_Cross(cam->right, cam->forward));

    cam->speed = 50000.0f;
    cam->sensitivity = 0.002f;
    cam->space_mode = true;
    cam->space_up = cam->up;
    cam->space_forward = cam->forward;

    cam->terrain = terrain;
    cam->planet_radius = planet_radius;
    cam->min_altitude = 5.0f;  // 5m clearance above terrain
}

static void update_space_mode(Camera* cam, float dt) {
    float dyaw = -cam->mouse_dx_accum * cam->sensitivity;
    float dpitch = -cam->mouse_dy_accum * cam->sensitivity;
    cam->mouse_dx_accum = 0.0f;
    cam->mouse_dy_accum = 0.0f;

    float droll = 0.0f;
    if (cam->key_q) droll -= 2.0f * dt;
    if (cam->key_e) droll += 2.0f * dt;

    // Yaw around up
    HMM_Mat4 yaw_rot = HMM_Rotate_RH(dyaw, cam->space_up);
    cam->space_forward = HMM_NormV3(HMM_MulM4V4(yaw_rot, HMM_V4V(cam->space_forward, 0.0f)).XYZ);

    HMM_Vec3 right = HMM_NormV3(HMM_Cross(cam->space_forward, cam->space_up));

    // Pitch around right
    HMM_Mat4 pitch_rot = HMM_Rotate_RH(dpitch, right);
    cam->space_forward = HMM_NormV3(HMM_MulM4V4(pitch_rot, HMM_V4V(cam->space_forward, 0.0f)).XYZ);
    cam->space_up = HMM_NormV3(HMM_MulM4V4(pitch_rot, HMM_V4V(cam->space_up, 0.0f)).XYZ);

    // Roll around forward
    if (droll != 0.0f) {
        HMM_Mat4 roll_rot = HMM_Rotate_RH(droll, cam->space_forward);
        cam->space_up = HMM_NormV3(HMM_MulM4V4(roll_rot, HMM_V4V(cam->space_up, 0.0f)).XYZ);
    }

    right = HMM_NormV3(HMM_Cross(cam->space_forward, cam->space_up));
    cam->forward = cam->space_forward;
    cam->right = right;
    cam->up = cam->space_up;

    // Movement (double precision accumulation)
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
        cam->pos_d[0] += (double)(move.X * cam->speed * dt);
        cam->pos_d[1] += (double)(move.Y * cam->speed * dt);
        cam->pos_d[2] += (double)(move.Z * cam->speed * dt);
    }

    cam->position = HMM_V3((float)cam->pos_d[0], (float)cam->pos_d[1], (float)cam->pos_d[2]);
}

void camera_update(Camera* cam, float dt, double planet_radius) {
    if (!cam->mouse_locked) {
        cam->mouse_dx_accum = 0.0f;
        cam->mouse_dy_accum = 0.0f;
    }

    // Height-based speed: scales from ~10 m/s at surface to ~500 km/s in high orbit
    // altitude_above_surface is in meters
    double dist_from_center = sqrt(cam->pos_d[0]*cam->pos_d[0] +
                                   cam->pos_d[1]*cam->pos_d[1] +
                                   cam->pos_d[2]*cam->pos_d[2]);
    double altitude_above_surface = dist_from_center - (double)planet_radius;
    if (altitude_above_surface < 0.0) altitude_above_surface = 0.0;

    // Logarithmic speed curve: at 0m -> 10 m/s, at 1km -> ~100 m/s,
    // at 100km -> ~50,000 m/s, at 1600km (2x radius) -> ~500,000 m/s
    float base_speed = 10.0f + (float)(altitude_above_surface * 0.3);
    if (base_speed > 500000.0f) base_speed = 500000.0f;
    cam->speed = base_speed;

    update_space_mode(cam, dt);

    // Terrain collision: sample terrain height at camera position, push above it
    if (cam->terrain) {
        double cam_dist = sqrt(cam->pos_d[0]*cam->pos_d[0] +
                               cam->pos_d[1]*cam->pos_d[1] +
                               cam->pos_d[2]*cam->pos_d[2]);
        if (cam_dist > 0.001) {
            // Unit direction from planet center
            float ux = (float)(cam->pos_d[0] / cam_dist);
            float uy = (float)(cam->pos_d[1] / cam_dist);
            float uz = (float)(cam->pos_d[2] / cam_dist);

            // Sample terrain height at this point on the sphere
            float terrain_h = terrain_sample_height_m(cam->terrain, ux, uy, uz);
            float terrain_radius = cam->planet_radius + terrain_h;
            float min_radius = terrain_radius + cam->min_altitude;

            cam->altitude = (float)(cam_dist - (double)terrain_radius);

            if ((float)cam_dist < min_radius) {
                // Push camera out to minimum altitude
                double scale = (double)min_radius / cam_dist;
                cam->pos_d[0] *= scale;
                cam->pos_d[1] *= scale;
                cam->pos_d[2] *= scale;
                cam->altitude = cam->min_altitude;
            }
        }

        cam->position = HMM_V3((float)cam->pos_d[0], (float)cam->pos_d[1], (float)cam->pos_d[2]);
    }

    // Local up = radial direction on sphere
    float pos_len = HMM_LenV3(cam->position);
    if (pos_len > 0.001f) {
        cam->local_up = HMM_MulV3F(cam->position, 1.0f / pos_len);
    }

    // Build view matrix DIRECTLY from basis vectors (matches hex-planets).
    // AVOID HMM_LookAt_RH — it does normalize(target-eye) internally,
    // which loses precision at 800km due to float cancellation.
    {
        HMM_Vec3 F = cam->forward;
        HMM_Vec3 S = HMM_NormV3(HMM_Cross(F, cam->up));
        HMM_Vec3 U = HMM_Cross(S, F);

        cam->view.Elements[0][0] = S.X;
        cam->view.Elements[0][1] = U.X;
        cam->view.Elements[0][2] = -F.X;
        cam->view.Elements[0][3] = 0.0f;

        cam->view.Elements[1][0] = S.Y;
        cam->view.Elements[1][1] = U.Y;
        cam->view.Elements[1][2] = -F.Y;
        cam->view.Elements[1][3] = 0.0f;

        cam->view.Elements[2][0] = S.Z;
        cam->view.Elements[2][1] = U.Z;
        cam->view.Elements[2][2] = -F.Z;
        cam->view.Elements[2][3] = 0.0f;

        // Translation row (will be zeroed for camera-relative rendering in render.c)
        cam->view.Elements[3][0] = -HMM_DotV3(S, cam->position);
        cam->view.Elements[3][1] = -HMM_DotV3(U, cam->position);
        cam->view.Elements[3][2] = HMM_DotV3(F, cam->position);
        cam->view.Elements[3][3] = 1.0f;
    }

    // Projection: RH_NO for ALL backends (log depth overrides z anyway)
    // Matches hex-planets: 70 FOV, 0.01 near, 10M far
    float aspect = sapp_widthf() / sapp_heightf();
    cam->proj = HMM_Perspective_RH_NO(HMM_AngleDeg(70.0f), aspect, 0.01f, 10000000.0f);
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

    // Scroll wheel adjusts min_altitude (clearance above terrain)
    if (ev->type == SAPP_EVENTTYPE_MOUSE_SCROLL) {
        cam->min_altitude *= (ev->scroll_y > 0) ? 1.25f : 0.8f;
        if (cam->min_altitude < 2.0f) cam->min_altitude = 2.0f;
        if (cam->min_altitude > 10000.0f) cam->min_altitude = 10000.0f;
    }
}
