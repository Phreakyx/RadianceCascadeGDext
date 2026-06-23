#[compute]
#version 450

#include "rc_light_eval_inc.glslinc"

layout(local_size_x = 4, local_size_y = 4, local_size_z = 4) in;

layout(set = 0, binding = 0, rgba16f) uniform image3D          radiance;
layout(set = 0, binding = 1, rgba8)   uniform readonly image3D albedo_in;
layout(set = 0, binding = 2, rgba8)   uniform readonly image3D normal_in;
layout(set = 0, binding = 3)          uniform sampler3D         sdf_tex;
layout(set = 0, binding = 4, rgba16f) uniform readonly image3D emission_in;
layout(set = 0, binding = 5, std430) readonly buffer Lights { RCLight lights[]; };

layout(push_constant) uniform PC {
    vec3  vox_origin; uint  light_count;
    float voxel_size; float blend_alpha; uint _pf, _pg;
    ivec3 slab_lo;    uint  res;
    ivec3 slab_dim;   uint  _p2;
    ivec3 phase;      uint  _p3;
} pc;

ivec3 rel(ivec3 cell) { int R = int(pc.res); return ((cell - pc.phase) % R + R) % R; }

// Soft SDF visibility, window-relative voxels, marching toward L up to reach_vox (≤ res).
// Directional: reach = res (window edge = sky). Omni/spot: reach = distance-to-light in voxels.
float light_vis(vec3 rstart, vec3 N, vec3 L, float reach_vox) {
    vec3  rp = rstart + N * 1.5;
    float t = 1.0, vis = 1.0; const float k = 16.0; float R = float(pc.res);
    for (int i = 0; i < 96 && t < reach_vox; ++i) {
        vec3 r = rp + L * t;
        if (any(lessThan(r, vec3(0.0))) || any(greaterThan(r, vec3(R)))) return vis;  // left window → unoccluded
        vec3 uvw = fract((r + vec3(pc.phase)) / R);
        float d = textureLod(sdf_tex, uvw, 0.0).r;
        if (d < 0.5) return 0.0;                                                       // occluder → shadow
        vis = min(vis, k * d / t);
        t += max(d, 1.0);
    }
    return clamp(vis, 0.0, 1.0);
}

void main() {
    ivec3 lid = ivec3(gl_GlobalInvocationID);
    if (any(greaterThanEqual(lid, pc.slab_dim))) return;
    ivec3 wv   = pc.slab_lo + lid;
    ivec3 cell = ((wv % int(pc.res)) + int(pc.res)) % int(pc.res);

    vec4 rad = imageLoad(radiance, cell);
    if (rad.a < 0.5) return;
    vec3 N   = normalize(imageLoad(normal_in, cell).rgb * 2.0 - 1.0);
    vec3 alb = imageLoad(albedo_in, cell).rgb;
    vec3 em  = imageLoad(emission_in, cell).rgb;

    vec3  rstart = vec3(rel(cell)) + 0.5;
    vec3  W      = (vec3(wv) + 0.5) * pc.voxel_size;   // KEEP the fix — no vox_origin
    float R      = float(pc.res);

    vec3 Lo = em;
    for (uint i = 0u; i < pc.light_count; ++i) {
        vec3 Ldir, radiance; float reach;
        if (!rc_eval_light(lights[i], W, N, alb, pc.voxel_size, R, Ldir, radiance, reach)) continue;
        float vis = light_vis(rstart, N, Ldir, reach);
        Lo += radiance * vis;
    }
    rad.rgb = mix(rad.rgb, Lo, pc.blend_alpha);
    imageStore(radiance, cell, rad);
}