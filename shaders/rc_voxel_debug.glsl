#[compute]
#version 450

// DEBUG_VOXEL — march a primary ray per pixel through the voxel grid and shade the
// first solid voxel by a gradient-estimated normal (so the voxelized scene reads as
// solid relief), adding captured emission from the dedicated EMISSION grid. Lets us
// verify the mesh voxelizer captured the geometry, scale, occupancy AND emission.
// (Emission must come from emission_tex, NOT voxel_tex.rgb — the mesh voxelizer writes
//  rgb=0 to voxel_tex and routes emission to its own texture; at level 0 voxel_tex.rgb
//  is the lit-radiance grid, which is 0 pre-inject and would hide emitters here.)

layout(local_size_x = 8, local_size_y = 8) in;

layout(set = 0, binding = 0) uniform sampler3D voxel_tex;
layout(set = 0, binding = 1, rgba16f) uniform writeonly image2D debug_out;
layout(set = 0, binding = 2, std140) uniform CameraData {
    mat4 inv_proj; mat4 inv_view; mat4 fwd_proj; mat4 fwd_view;
    vec2 jitter; vec2 _pad;
} cam;
// L0: the dedicated emission grid (rgb). Clip levels: the level's own grid (rgb = baked
// radiance+emission), since coarse levels store emission folded into the grid colour.
layout(set = 0, binding = 3) uniform sampler3D emission_tex;

layout(push_constant) uniform PC {
    uint  screen_width, screen_height, res, max_steps;
    vec3  vox_origin;  float voxel_size;
    vec3  vox_extent;  float occ_threshold;
} pc;

float occ_at(vec3 world) {
    vec3 gate = (world - pc.vox_origin) / pc.vox_extent;          // GATE
    if (any(lessThan(gate, vec3(0.0))) || any(greaterThan(gate, vec3(1.0)))) return 0.0;
    return textureLod(voxel_tex, fract(world / pc.vox_extent), 0.0).a;   // SAMPLE (toroidal)
}

void main() {
    ivec2 px = ivec2(gl_GlobalInvocationID.xy);
    if (px.x >= int(pc.screen_width) || px.y >= int(pc.screen_height)) return;

    // primary ray from the camera through this pixel
    vec2 ndc = (vec2(px) + 0.5) / vec2(pc.screen_width, pc.screen_height) * 2.0 - 1.0;
    ndc.y = -ndc.y;
    vec4 vh  = cam.inv_proj * vec4(ndc, 1.0, 1.0);
    vec3 dir = normalize((cam.inv_view * vec4(normalize(vh.xyz / vh.w), 0.0)).xyz);
    vec3 ro  = (cam.inv_view * vec4(0.0, 0.0, 0.0, 1.0)).xyz;

    float t = 0.0;
    for (uint i = 0u; i < pc.max_steps; ++i) {
        vec3 p = ro + dir * t;
        if (occ_at(p) > pc.occ_threshold) {
            // gradient normal from occupancy
            float h = pc.voxel_size;
            vec3 n = normalize(vec3(
                occ_at(p + vec3(h,0,0)) - occ_at(p - vec3(h,0,0)),
                occ_at(p + vec3(0,h,0)) - occ_at(p - vec3(0,h,0)),
                occ_at(p + vec3(0,0,h)) - occ_at(p - vec3(0,0,h))));
            if (any(isnan(n))) n = -dir;
            vec3 emis = textureLod(emission_tex, fract(p / pc.vox_extent), 0.0).rgb;
            float ndl  = max(dot(n, normalize(vec3(0.5, 0.8, 0.3))), 0.0);
            vec3  shade = vec3(0.18 + 0.6 * ndl);              // gray relief
            imageStore(debug_out, px, vec4(shade + emis, 1.0));
            return;
        }
        t += pc.voxel_size;                                    // mip-0 step
    }
    imageStore(debug_out, px, vec4(0.0));                      // miss → black
}
