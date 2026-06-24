#[compute]
#version 450

// Sparse-RC (cascaded, NON-SHARED) — clear pass.
// PERSISTENT BUCKETS: the hash table is NOT wiped each frame (a probe keeps its slot + radiance so
// temporal amortization has a stable home and re-seeding never churns existence). Only the per-cascade
// live counters are reset — the live list is rebuilt by the add pass each frame. Stale slots are reclaimed
// by the separate evict pass; buckets are initialised to EMPTY once, at buffer creation.

layout(local_size_x = 256) in;

layout(set = 0, binding = 0, std430) coherent buffer Buckets { uvec2 buckets[]; };   // bound but NOT written
layout(set = 0, binding = 1, std430) coherent buffer Alloc   { uint  alloc_count[]; };

layout(push_constant) uniform PC { uint total_buckets; uint num_cascades; uint _b, _c; } pc;

void main() {
    uint i = gl_GlobalInvocationID.x;
    if (i < pc.num_cascades) alloc_count[i] = 0u;   // buckets persist; only the live counters reset
}
