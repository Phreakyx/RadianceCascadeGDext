#[compute]
#version 450

// Sparse-RC (cascaded, NON-SHARED) — clear pass.
// One contiguous Buckets buffer holds all N per-cascade hash regions back-to-back;
// Alloc is now an array of N counters (one live probe count per cascade). Wipe both.

layout(local_size_x = 256) in;

layout(set = 0, binding = 0, std430) coherent buffer Buckets { uvec2 buckets[]; };
layout(set = 0, binding = 1, std430) coherent buffer Alloc   { uint  alloc_count[]; };

layout(push_constant) uniform PC { uint total_buckets; uint num_cascades; uint _b, _c; } pc;

void main() {
    uint i = gl_GlobalInvocationID.x;
    if (i < pc.total_buckets) buckets[i] = uvec2(0xffffffffu, 0xffffffffu);  // EMPTY, INVALID
    if (i < pc.num_cascades)  alloc_count[i] = 0u;
}
