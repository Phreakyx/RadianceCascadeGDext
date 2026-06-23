#[compute]
#version 450

#include "rc_light_eval_inc.glslinc"

// Coarse clipmap inject, SLAB form. One thread per shell voxel: rgb = emission +
// albedo·sun·max(N·L,0)/π, a = occupancy. No SDF march (vis = 1) — far-field sun shadows
// are below coarse-voxel perceptibility; level 0 carries the sharp shadows.

layout(local_size_x = 4, local_size_y = 4, local_size_z = 4) in;

layout(set = 0, binding = 0, rgba16f) uniform image3D          radiance;     // in .a=occ, out rgb+occ
layout(set = 0, binding = 1, rgba8)   uniform readonly image3D albedo_in;
layout(set = 0, binding = 2, rgba8)   uniform readonly image3D normal_in;
layout(set = 0, binding = 3, rgba16f) uniform readonly image3D emission_in;
layout(set = 0, binding = 4, std430)  readonly buffer Lights { RCLight lights[]; };

layout(push_constant) uniform PC {
    vec3 sun_dir;   float blend_alpha;
    vec3 sun_color; float voxel_size;   // was _p1
    ivec3 slab_lo;  uint  res;
    ivec3 slab_dim; uint  light_count;  // was _p2
    ivec3 phase;    uint  _p3;
} pc;

ivec3 rel(ivec3 cell) { int R = int(pc.res); return ((cell - pc.phase) % R + R) % R; }

// Coarse occupancy visibility, window-relative voxels, toward L up to reach_vox (≤32 steps).
float clip_vis(vec3 rstart, vec3 L, float reach_vox) {
    float R = float(pc.res);
    vec3  rp = rstart + L * 1.5;
    int   steps = int(min(reach_vox, 32.0));
    for (int i = 0; i < steps; ++i) {
        vec3 r = rp + L * float(i);
        if (any(lessThan(r, vec3(0.0))) || any(greaterThanEqual(r, vec3(R)))) return 1.0;  // exited → lit
        ivec3 cell = ((ivec3(floor(r)) + pc.phase) % int(R) + int(R)) % int(R);
        if (imageLoad(radiance, cell).a > 0.5) return 0.0;                                  // occluder → shadow
    }
    return 1.0;
}

void main() {
    ivec3 lid = ivec3(gl_GlobalInvocationID);
    if (any(greaterThanEqual(lid, pc.slab_dim))) return;
    ivec3 wv   = pc.slab_lo + lid;
    ivec3 cell = ((wv % int(pc.res)) + int(pc.res)) % int(pc.res);   // toroidal

    vec4 rad = imageLoad(radiance, cell);
    if (rad.a < 0.5) return;                                          // empty voxel

    vec3  N   = normalize(imageLoad(normal_in, cell).rgb * 2.0 - 1.0);
    vec3  alb = imageLoad(albedo_in, cell).rgb;
    vec3  em  = imageLoad(emission_in, cell).rgb;
    vec3  L   = normalize(pc.sun_dir);
    float ndl = max(dot(N, L), 0.0);

    float R       = float(pc.res);
    vec3  rstart  = vec3(rel(cell)) + 0.5;
    vec3  W       = (vec3(wv) + 0.5) * pc.voxel_size;
    float svis    = (ndl > 0.0) ? clip_vis(rstart, L, R) : 0.0;

    vec3 Lo = em + alb * pc.sun_color * (ndl * svis * 0.31830988618);

    for (uint i = 0u; i < pc.light_count; ++i) {           // positional only; directional = sun above
        if (lights[i].type < 0.5) continue;
        vec3 Ldir, radiance; float reach;
        if (!rc_eval_light(lights[i], W, N, alb, pc.voxel_size, R, Ldir, radiance, reach)) continue;
        Lo += radiance * clip_vis(rstart, Ldir, reach);
    }
    rad.rgb = mix(rad.rgb, Lo, pc.blend_alpha);
    imageStore(radiance, cell, rad);
}