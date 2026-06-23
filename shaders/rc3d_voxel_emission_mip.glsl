#[compute]
#version 450

// Isotropic emission mip. Emission radiates equally in all directions and does NOT
// occlude itself, so it downsamples as a plain occupancy-weighted mean — no front-to-back
// gating (that's only for reflected radiance in the aniso mip). The trace adds this
// ungated, so an emissive cube broadcasts its glow outward through coarse levels instead
// of being sealed behind its own faces.
//
// .rgb = emission radiance,  .a = occupancy (mean, for the weighted average only).

layout(local_size_x = 4, local_size_y = 4, local_size_z = 4) in;

layout(set = 0, binding = 0) uniform sampler3D src;                  // finer emission level
layout(set = 0, binding = 1, rgba16f) uniform writeonly image3D dst; // coarser emission level

layout(push_constant) uniform PC {
    uint dst_res;
    uint _p0; uint _p1; uint _p2;
} pc;

void main() {
    ivec3 d = ivec3(gl_GlobalInvocationID);
    if (any(greaterThanEqual(d, ivec3(int(pc.dst_res))))) return;

    ivec3 b = d * 2;
    vec3  rgb  = vec3(0.0);
    float asum = 0.0;
    for (int z = 0; z < 2; ++z)
    for (int y = 0; y < 2; ++y)
    for (int x = 0; x < 2; ++x) {
        vec4 s = texelFetch(src, b + ivec3(x, y, z), 0);
        rgb  += s.rgb * s.a;     // occupancy-weighted so empty children don't dilute emitters
        asum += s.a;
    }
    // normalize radiance by the emitting occupancy (not by 8) so a partly-filled coarse cell
    // keeps the emitter's intensity instead of being darkened by empty siblings
    vec3 out_rgb = (asum > 1e-4) ? rgb / asum : vec3(0.0);
    float out_a  = asum * 0.125;
    imageStore(dst, d, vec4(out_rgb, out_a));
}
