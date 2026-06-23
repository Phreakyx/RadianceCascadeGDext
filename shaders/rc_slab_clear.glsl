#[compute]
#version 450
layout(local_size_x = 4, local_size_y = 4, local_size_z = 4) in;

layout(set = 0, binding = 0, rgba16f) uniform image3D voxel_out;
layout(set = 0, binding = 1, rgba8)   uniform image3D albedo_out;
layout(set = 0, binding = 2, rgba8)   uniform image3D normal_out;
layout(set = 0, binding = 3, rgba16f) uniform image3D emission_out;

layout(push_constant) uniform PC {
    ivec3 slab_lo;  uint res;
    ivec3 slab_dim; uint _p;
} pc;

void main() {
    ivec3 lid = ivec3(gl_GlobalInvocationID);
    if (any(greaterThanEqual(lid, pc.slab_dim))) return;
    ivec3 wv   = pc.slab_lo + lid;
    ivec3 cell = ((wv % int(pc.res)) + int(pc.res)) % int(pc.res);
    imageStore(voxel_out,    cell, vec4(0.0));
    imageStore(albedo_out,   cell, vec4(0.0));
    imageStore(normal_out,   cell, vec4(0.0));
    imageStore(emission_out, cell, vec4(0.0));
}