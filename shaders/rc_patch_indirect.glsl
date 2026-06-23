#[compute]
#version 450

// Sparse-RC (cascaded, NON-SHARED) — build indirect dispatch args. One thread per cascade.
// TRACE and MERGE both iterate the COMPACT live list (live_list[0..alloc_count); see
// rc_patch_trace / rc_patch_merge), so both dispatches are sized to the live probe count.
// TRACE is per (probe,dir); MERGE is per probe (it loops dirs internally).
//   block 0 (cascades 0..N-1): TRACE  groups = ceil(alloc_count[c] * dirs / local_size)
//   block 1 (cascades 0..N-1): MERGE  groups = ceil(alloc_count[c]        / local_size)

layout(local_size_x = 16) in;

layout(set = 0, binding = 0, std430) readonly buffer Alloc { uint alloc_count[]; };
layout(set = 0, binding = 1, std430) writeonly buffer Indirect { uint groups[]; };   // 2N * 3 uints

struct CascadeDesc {
    float spacing; float t_start; float t_end; float aperture;
    uint  dirs;    uint  oct_res; uint  bucket_off; uint  bucket_cap;
    uint  probe_off; uint probe_cap; uint rad_off; uint _p0;
};
layout(set = 0, binding = 2, std430) readonly buffer Cascades { CascadeDesc cascades[]; };

layout(push_constant) uniform PC { uint num_cascades; uint local_size; uint _b, _c; } pc;

void main() {
    uint c = gl_GlobalInvocationID.x;
    if (c >= pc.num_cascades) return;
    CascadeDesc cd = cascades[c];

    uint tn = alloc_count[c] * cd.dirs;                          // TRACE: live probes × dirs (compact)
    groups[c*3u + 0u] = (tn + pc.local_size - 1u) / pc.local_size;
    groups[c*3u + 1u] = 1u;
    groups[c*3u + 2u] = 1u;

    uint base = pc.num_cascades * 3u;                            // MERGE: live probes (compact, per-probe)
    groups[base + c*3u + 0u] = (alloc_count[c] + pc.local_size - 1u) / pc.local_size;
    groups[base + c*3u + 1u] = 1u;
    groups[base + c*3u + 2u] = 1u;
}
