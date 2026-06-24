#[compute]
#version 450

// Sparse-RC (cascaded, NON-SHARED) — MERGE NEIGHBOUR PRECOMPUTE.
// For each LIVE cascade-c probe, find the 8 trilinear cascade-(c+1) neighbour dense ids ONCE and store
// them in `neighbours[idx*8 + o]`. The merge then reads those 8 ids directly instead of doing the
// register-heavy find_in_region 64-probe hash loop inline.
//
// WHY (Nsight 2026-06-24): the frame is latency-limited by Long Scoreboard (memory-read latency), and it
// is NOT hidden because CS occupancy is ~22%, limited by REGISTER PRESSURE — not by cache misses (L2 hit
// 95%). Merge carries find_in_region + the 8-neighbour trilinear + the per-dir accumulation in one
// register-hungry kernel. Hoisting the hash probes into this lean, high-occupancy pass (its own load
// latency hidden by its own occupancy) drops merge's register count → more resident merge warps → merge
// hides its radiance-load latency. Mirrors merge's find_in_region EXACTLY so the two can't diverge.

layout(local_size_x = 64) in;

layout(set = 0, binding = 0, std430) readonly  buffer Buckets    { uvec2 buckets[]; };
layout(set = 0, binding = 1, std430) readonly  buffer Alloc      { uint  alloc_count[]; };
layout(set = 0, binding = 2, std430) readonly  buffer ProbeKeys  { ivec4 probe_keys[]; };
layout(set = 0, binding = 3, std430) readonly  buffer ProbeData  { vec4  probe_world[]; };
layout(set = 0, binding = 4, std430) readonly  buffer LiveList   { uint  live_list[]; };
layout(set = 0, binding = 9, std430) writeonly buffer Neighbours { uint  neighbours[]; };

struct CascadeDesc {
    float spacing; float t_start; float t_end; float aperture;
    uint  dirs;    uint  oct_res; uint  bucket_off; uint  bucket_cap;
    uint  probe_off; uint probe_cap; uint rad_off; uint _p0;
};
layout(set = 0, binding = 7, std430) readonly buffer Cascades { CascadeDesc cascades[]; };

layout(push_constant) uniform PC { uint cascade; uint _p0, _p1, _p2; } pc;

const uint EMPTY = 0xffffffffu, INVALID = 0xffffffffu, MAX_LINEAR = 64u;

uint hash_ivec4(ivec4 k) {
    uint h = uint(k.x)*73856093u ^ uint(k.y)*19349663u ^ uint(k.z)*83492791u ^ uint(k.w)*2654435761u;
    h ^= h>>15; h *= 2246822519u; h ^= h>>13; return h;
}
uint find_in_region(ivec4 key, uint boff, uint bcap) {
    uint h = hash_ivec4(key); if (h >= 0xfffffffeu) h = 1u;
    uint slot = boff + (h % bcap);
    for (uint p = 0u; p < MAX_LINEAR; ++p) {
        uvec2 b = buckets[slot];
        if (b.x == EMPTY) return INVALID;
        if (b.x == h && b.y != INVALID && probe_keys[b.y] == key) return b.y;
        slot = boff + ((slot - boff + 1u) % bcap);
    }
    return INVALID;
}

void main() {
    uint c = pc.cascade;
    CascadeDesc cd = cascades[c];
    CascadeDesc cn = cascades[c + 1u];

    uint i = gl_GlobalInvocationID.x;
    if (i >= alloc_count[c]) return;                        // past live list (dispatch rounding)
    uint slot_local = live_list[cd.probe_off + i] & 0x7fffffffu;
    uint idx        = cd.probe_off + slot_local;
    vec3 W          = probe_world[idx].xyz;

    vec3  sp = W / cn.spacing - 0.5;
    ivec3 b  = ivec3(floor(sp));
    for (int o = 0; o < 8; ++o) {
        ivec3 cell = b + ivec3(o & 1, (o >> 1) & 1, (o >> 2) & 1);
        neighbours[idx * 8u + uint(o)] = find_in_region(ivec4(cell, int(c + 1u)), cn.bucket_off, cn.bucket_cap);
    }
}
