// planet.glsl -- Planet terrain shader
// Camera-relative rendering + logarithmic depth. Terrain only (no atmosphere).
@ctype mat4 HMM_Mat4
@ctype vec4 HMM_Vec4

@vs planet_vs
layout(binding=0) uniform planet_vs_params {
    mat4 mvp;
    vec4 camera_offset;
    vec4 camera_offset_low;
    vec4 log_depth;
};

layout(location=0) in vec3 a_position;
layout(location=1) in vec3 a_normal;
layout(location=2) in vec3 a_color;

out vec3 fs_normal;
out vec3 fs_color;
out vec3 fs_cam_rel_pos;

void main() {
    vec3 cam_rel_pos = (a_position - camera_offset.xyz) - camera_offset_low.xyz;
    gl_Position = mvp * vec4(cam_rel_pos, 1.0);

    float Fcoef = log_depth.x;
    if (Fcoef > 0.0) {
        float w = gl_Position.w;
        gl_Position.z = (log2(max(1e-6, 1.0 + w)) * Fcoef + log_depth.z) * w;
    }

    fs_normal = a_normal;
    fs_color = a_color;
    fs_cam_rel_pos = cam_rel_pos;
}
@end

@fs planet_fs
layout(binding=1) uniform planet_fs_params {
    vec4 sun_direction;  // xyz = sun dir
    vec4 camera_pos;     // xyz = camera world pos
    vec4 atmos_params;   // unused for now
    vec4 lod_debug;      // x = depth (0=off), y = max_depth
};

in vec3 fs_normal;
in vec3 fs_color;
in vec3 fs_cam_rel_pos;

out vec4 frag_color;

void main() {
    vec3 N = normalize(fs_normal);
    vec3 L = normalize(sun_direction.xyz);

    vec3 world_pos_approx = fs_cam_rel_pos + camera_pos.xyz;
    vec3 surface_dir = normalize(world_pos_approx);

    float sun_facing = dot(surface_dir, L);
    float sun_brightness = smoothstep(-0.1, 0.3, sun_facing);

    float ndotl = max(0.0, dot(N, L));

    vec3 ambient = fs_color * 0.15;
    vec3 diffuse = fs_color * ndotl * sun_brightness * 0.85;
    vec3 color = ambient + diffuse;

    // LOD debug: color by depth level (red → yellow → green → cyan → blue)
    if (lod_debug.x > 0.0) {
        float t = clamp(lod_debug.x / max(lod_debug.y, 1.0), 0.0, 1.0);
        vec3 debug_color;
        debug_color.r = clamp(1.0 - t * 2.0, 0.0, 1.0) + clamp(t * 4.0 - 3.0, 0.0, 1.0);
        debug_color.g = clamp(t * 2.0, 0.0, 1.0) - clamp(t * 2.0 - 1.0, 0.0, 1.0);
        debug_color.b = clamp(t * 2.0 - 1.0, 0.0, 1.0);
        // Mix debug color with lighting so terrain shape is still visible
        color = debug_color * (0.3 + ndotl * sun_brightness * 0.7);
    }

    frag_color = vec4(color, 1.0);
}
@end

@program planet planet_vs planet_fs
