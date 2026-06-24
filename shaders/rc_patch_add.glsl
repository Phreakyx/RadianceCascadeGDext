#[compute]
#version 450

// Sparse-RC (cascaded, NON-SHARED) — CREATE pass. DETERMINISTIC slot-keyed probes.
//
// TWO-PHASE, RACE-INDEPENDENT OWNERSHIP. The old single-pass version claimed a slot with the
// first thread to atomicCompSwap an EMPTY slot — so when two cells collided at a home slot, WHICH
// cell got the home vs the linear-probe slot depended on thread order, and the assignment could
// flip frame to frame. Under temporal amortization that slot-churn was fatal: a colliding cell
// bounced between two storage slots, neither of which stayed put long enough (~N frames) for the
// rotating directional refresh to converge, so its radiance flickered between two half-stale
// states (and propagated up the merge continuation onto the floor).
//
//   phase 0 (CLAIM):  every cell does atomicMin(buckets[home].x, h). atomicMin is commutative, so
//                     the LOWEST-hash cell contending a home slot deterministically owns it,
//                     independent of thread order.
//   phase 1 (COMMIT): the home owner (buckets[home].x == h) commits at home; a loser (higher-hash
//                     cell that lost its home) deterministically skips to home+1 and linear-probes
//                     for an empty slot. A barrier between the phases guarantees every claim has
//                     landed before any commit reads buckets.x. For a 2-cell collision the lower-
//                     hash cell ALWAYS wins `home` and the other ALWAYS lands one slot up — same
//                     slots every frame → amortization converges → no flicker. (3+ cells piling on
//                     one home can still race for the overflow slots; rare at load factor 0.5.)
//
// CONTENTION: every pixel inserts 8 cells × N cascades; a coarse cell is touched by thousands of
// pixels, all running the SAME cell (same h/key/idx). Claim is idempotent (atomicMin). Commit
// dedups via atomicCompSwap on buckets[slot].y — the single winner appends to live_list and does
// the rad_tag bootstrap; probe_keys/world are written identically by every candidate so a torn
// read is harmless, and they are published before .y so a reader that sees .y has a valid key.

layout(local_size_x = 8, local_size_y = 8) in;

layout(set = 0, binding = 0, std430) coherent buffer Buckets   { uvec2 buckets[]; };
layout(set = 0, binding = 1, std430) coherent buffer Alloc     { uint  alloc_count[]; };
layout(set = 0, binding = 2, std430) coherent buffer ProbeKeys { ivec4 probe_keys[]; };
layout(set = 0, binding = 3, std430) coherent buffer ProbeData { vec4  probe_world[]; };   // xyz center, w cascade
layout(set = 0, binding = 4, std430) writeonly buffer LiveList { uint live_list[]; };
layout(set = 0, binding = 8, std430) coherent  buffer RadTag   { uint rad_tag[]; };   // per-slot owner hash, persisted across frames (NOT cleared) for temporal amortization
layout(set = 0, binding = 5, std140) uniform CameraData {
    mat4 inv_proj; mat4 inv_view; mat4 fwd_proj; mat4 fwd_view; vec2 jitter; vec2 _pad;
} cam;

struct CascadeDesc {
    float spacing; float t_start; float t_end; float aperture;
    uint  dirs;    uint  oct_res; uint  bucket_off; uint  bucket_cap;
    uint  probe_off; uint probe_cap; uint rad_off; uint _p0;
};
layout(set = 0, binding = 7, std430) readonly buffer Cascades { CascadeDesc cascades[]; };

layout(set = 1, binding = 0) uniform sampler2D depth_input;

layout(push_constant) uniform PC {
    uint  screen_width, screen_height, cascade_begin, cascade_end;
    float z_near, z_far; uint phase, _p2;     // phase: 0 = claim (atomicMin home), 1 = commit
} pc;

const uint EMPTY = 0xffffffffu, INVALID = 0xffffffffu, MAX_LINEAR = 64u;

float linearize_depth(float raw) { return (raw < 0.00001) ? pc.z_far : pc.z_near / raw; }
vec3 screen_to_world(vec2 sc, float lin) {
    vec2 ndc = (sc + 0.5) / vec2(pc.screen_width, pc.screen_height) * 2.0 - 1.0;
    ndc.y = -ndc.y;
    vec4 vh = cam.inv_proj * vec4(ndc, 1.0, 1.0);
    vec3 vd = vh.xyz / vh.w;
    return (cam.inv_view * vec4(vd * (lin / -vd.z), 1.0)).xyz;
}
uint hash_ivec4(ivec4 k) {
    uint h = uint(k.x)*73856093u ^ uint(k.y)*19349663u ^ uint(k.z)*83492791u ^ uint(k.w)*2654435761u;
    h ^= h>>15; h *= 2246822519u; h ^= h>>13; return h;
}

// PHASE 0 — reserve the home slot for the lowest-hash contender (order-independent).
void claim(uint c, ivec3 cell) {
    CascadeDesc cd = cascades[c];
    ivec4 key  = ivec4(cell, int(c));
    uint  h    = hash_ivec4(key); if (h == EMPTY) h = 1u;
    uint  home = cd.bucket_off + (h % cd.bucket_cap);
    atomicMin(buckets[home].x, h);
}

// Publish probe `idx` at `slot`. Keys/world written by every candidate (identical → torn-safe) and
// published (barrier) before .y; the single atomicCompSwap winner on .y owns rad_tag + live_list.
void commit_data(uint c, CascadeDesc cd, uint slot, uint idx, uint h, ivec4 key, vec3 center) {
    probe_keys[idx]  = key;
    probe_world[idx] = vec4(center, float(c));
    memoryBarrierBuffer();                                       // key/world visible before idx is published via .y
    if (atomicCompSwap(buckets[slot].y, INVALID, idx) == INVALID) {
        uint boot   = (rad_tag[idx] != h) ? 0x80000000u : 0u;   // owner changed → bootstrap all dirs (live_list bit 31)
        rad_tag[idx] = h;
        uint live_i = atomicAdd(alloc_count[c], 1u);            // live count AND compact index
        live_list[cd.probe_off + live_i] = (slot - cd.bucket_off) | boot;
    }
}

// PHASE 1 — the home owner commits at home; a loser deterministically probes from home+1.
void commit(uint c, ivec3 cell, vec3 center) {
    CascadeDesc cd = cascades[c];
    ivec4 key  = ivec4(cell, int(c));
    uint  h    = hash_ivec4(key); if (h == EMPTY) h = 1u;
    uint  base = cd.bucket_off;
    uint  home = base + (h % cd.bucket_cap);
    // claim phase set buckets[home].x to the min hash that wants `home`. If that's me I own home;
    // otherwise I lost it (a lower-hash cell did) → skip home and linear-probe from home+1.
    uint pstart = (buckets[home].x == h) ? 0u : 1u;
    for (uint p = pstart; p < MAX_LINEAR; ++p) {
        uint slot  = base + ((home - base + p) % cd.bucket_cap);
        uint local = slot - base;
        uint idx   = cd.probe_off + local;
        uint cur   = buckets[slot].x;
        if (p > 0u && cur == EMPTY) {                  // loser: try to claim an empty overflow slot
            uint prev = atomicCompSwap(buckets[slot].x, EMPTY, h);
            cur = (prev == EMPTY) ? h : prev;          // won → it's mine; else the winner's hash
        }
        if (cur == h) {                                // mine: the home I own, or a slot I/my-sibling claimed
            uint existing = buckets[slot].y;
            if (existing == INVALID) { commit_data(c, cd, slot, idx, h, key, center); return; }
            if (probe_keys[existing] == key) return;   // already committed by a sibling pixel of this cell
            // same hash, different key (rare full-hash collision) → keep probing
        }
        // occupied by a different hash → next slot
    }
    // chain full (load factor too high) → cell dropped this frame; raise bucket_cap
}

void main() {
    ivec2 px = ivec2(gl_GlobalInvocationID.xy);
    if (px.x >= int(pc.screen_width) || px.y >= int(pc.screen_height)) return;
    vec2 uv = (vec2(px) + 0.5) / vec2(pc.screen_width, pc.screen_height);
    float raw = texture(depth_input, uv).r;
    if (raw < 0.00001) return;

    vec3 world = screen_to_world(vec2(px), linearize_depth(raw));

    for (uint c = pc.cascade_begin; c < pc.cascade_end; ++c) {
        float s = cascades[c].spacing;
        // create the 8 cells the GATHER will trilinear-read for this surface point
        // (gather bases at floor(world/s - 0.5); match it exactly so no neighbor is ever missing)
        vec3  sp = world / s - 0.5;
        ivec3 b  = ivec3(floor(sp));
        for (int o = 0; o < 8; ++o) {
            ivec3 cell = b + ivec3(o & 1, (o >> 1) & 1, (o >> 2) & 1);
            if (pc.phase == 0u) {
                claim(c, cell);
            } else {
                vec3 center = (vec3(cell) + 0.5) * s;
                commit(c, cell, center);
            }
        }
    }
}
