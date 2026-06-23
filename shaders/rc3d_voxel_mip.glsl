#[compute]
#version 450

// Build one mip level of the voxel grid for cone tracing. Radiance is averaged
// weighted by occupancy (so empty children don't darken the colour); the alpha
// channel averages straight, giving the fractional occupancy of the coarser voxel.

layout(local_size_x = 4, local_size_y = 4, local_size_z = 4) in;

layout(set = 0, binding = 0) uniform sampler3D src;                 // single-mip slice of level L-1
layout(set = 0, binding = 1, rgba16f) uniform writeonly image3D dst;// single-mip slice of level L

layout(push_constant) uniform PC {
    uint dst_res;     // resolution of the level being written (= res >> dst_mip)
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
        vec4 s = texelFetch(src, b + ivec3(x, y, z), 0);   // src is its own mip 0
        rgb  += s.rgb * s.a;     // occupancy-weighted radiance
        asum += s.a;
    }

    vec3  out_rgb = rgb * 0.125;
    float out_a   = asum * 0.125;                 // mean occupancy of the 8 children
    imageStore(dst, d, vec4(out_rgb, out_a));
}
