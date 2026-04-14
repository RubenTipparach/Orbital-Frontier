// planet.glsl -- Planet terrain shader
// Camera-relative rendering with logarithmic depth buffer for 64-bit precision.
@ctype mat4 HMM_Mat4
@ctype vec4 HMM_Vec4

@vs planet_vs
layout(binding=0) uniform planet_vs_params {
    mat4 mvp;
    vec4 camera_offset;      // xyz = camera position (float high part)
    vec4 camera_offset_low;  // xyz = residual from double split (low part)
    vec4 log_depth;          // x = Fcoef, y = far_plane, z = z_bias (-1 GL, 0 D3D/Metal/WebGPU)
};

layout(location=0) in vec3 a_position;
layout(location=1) in vec3 a_normal;
layout(location=2) in vec3 a_color;

out vec3 fs_normal;
out vec3 fs_color;
out vec3 fs_cam_rel_pos;

void main() {
    // Double-float camera-relative subtraction:
    // (a_position - offset_high) cancels large magnitudes for nearby verts
    // then offset_low adds back sub-ULP residual from the double split
    vec3 cam_rel_pos = (a_position - camera_offset.xyz) - camera_offset_low.xyz;
    gl_Position = mvp * vec4(cam_rel_pos, 1.0);

    // Logarithmic depth buffer
    // Maps depth to [0,1] for D3D11/Metal/WebGPU or [-1,1] for GL
    float Fcoef = log_depth.x;
    if (Fcoef > 0.0) {
        float w = gl_Position.w;
        float z_log = log2(max(1e-6, 1.0 + w)) * Fcoef;
        // z_bias = -1 for GL (remap [0,1] to [-1,1]), 0 for D3D11/Metal/WebGPU
        gl_Position.z = (z_log + log_depth.z) * w;
    }

    fs_normal = a_normal;
    fs_color = a_color;
    fs_cam_rel_pos = cam_rel_pos;
}
@end

@fs planet_fs
layout(binding=1) uniform planet_fs_params {
    vec4 sun_direction;  // xyz = normalized sun dir, w = unused
    vec4 camera_pos;     // xyz = camera world pos (float), w = unused
    vec4 atmos_params;   // x = planet_radius, y = atmos_radius, z = rayleigh_scale, w = sun_intensity
};

in vec3 fs_normal;
in vec3 fs_color;
in vec3 fs_cam_rel_pos;

out vec4 frag_color;

void main() {
    vec3 N = normalize(fs_normal);
    vec3 L = normalize(sun_direction.xyz);
    vec3 V = normalize(-fs_cam_rel_pos);

    // Surface direction (radial up)
    vec3 surface_dir = normalize(camera_pos.xyz + fs_cam_rel_pos);

    // Smooth terminator
    float NdotL = dot(N, L);
    float sun_brightness = smoothstep(-0.1, 0.3, NdotL);

    // Ambient occlusion from normal vs radial
    float normal_ao = smoothstep(-0.1, 0.5, dot(N, surface_dir));
    float ao = mix(0.45, 1.0, normal_ao);

    // Lambert diffuse
    float diffuse = max(0.0, NdotL);

    // Ambient
    vec3 ambient = fs_color * 0.08 * ao;
    vec3 lit = fs_color * diffuse * sun_brightness * ao;

    // Rim light
    float rim = pow(1.0 - max(0.0, dot(V, N)), 3.0) * 0.12;
    lit += vec3(rim) * sun_brightness;

    // Shadow desaturation
    float luminance = dot(lit + ambient, vec3(0.299, 0.587, 0.114));
    float desat_factor = 1.0 - sun_brightness * 0.3;
    vec3 color = mix(vec3(luminance), lit + ambient, desat_factor);

    // Aerial perspective (simplified inline fog)
    float dist = length(fs_cam_rel_pos);
    float planet_r = atmos_params.x;
    float atmos_r = atmos_params.y;
    float altitude = length(camera_pos.xyz) - planet_r;
    float atmos_height = atmos_r - planet_r;

    if (altitude < atmos_height) {
        float fog_density = exp(-altitude / (atmos_height * 0.15));
        float fog_amount = 1.0 - exp(-dist * fog_density * 0.00002);
        fog_amount = clamp(fog_amount, 0.0, 0.85);
        vec3 fog_color = vec3(0.4, 0.6, 1.0) * sun_brightness * 0.5;
        color = mix(color, fog_color, fog_amount);
    }

    frag_color = vec4(color, 1.0);
}
@end

@program planet planet_vs planet_fs
