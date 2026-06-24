#[compute]
#version 450

// Sparse-RC (cascaded, NON-SHARED) — clear pass.
// DENSE-POOL design: the hashmap is TRANSIENT — wipe it to EMPTY here, then the rebuild pass repopulates
// it from the persistent dense probe pool (probe identity is the stable dense id, not the slot). Also
// reset the per-cascade live counters (the live list is rebuilt by add). The dense pool itself
// (keys/world/radiance/rad_tag/last_seen/freelist/alloc_state) is NOT touched here — it persists.

layout(local_size_x = 256) in;

layout(set = 0, binding = 0, std430) coherent buffer Buckets { uvec2 buckets[]; };
layout(set = 0, binding = 1, std430) coherent buffer Alloc   { uint  alloc_count[]; };

layout(push_constant) uniform PC { uint total_buckets; uint num_cascades; uint _b, _c; } pc;

void main() {
    uint i = gl_GlobalInvocationID.x;
    if (i < pc.total_buckets) buckets[i] = uvec2(0xffffffffu, 0xffffffffu);   // EMPTY, INVALID
    if (i < pc.num_cascades)  alloc_count[i] = 0u;
}
