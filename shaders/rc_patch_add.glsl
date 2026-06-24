#[compute]
#version 450

// Sparse-RC (cascaded, NON-SHARED) — CREATE pass. DETERMINISTIC slot-keyed probes.
//
// DETERMINISTIC SORTED-CHAIN OWNERSHIP. The old single-CAS version claimed a slot with the first
// thread to atomicCompSwap an EMPTY slot — so when cells collided at a home slot, WHICH cell got
// home vs the overflow slots depended on thread order, and the assignment flipped frame to frame.
// The 2-phase atomicMin(home) version fixed 2-cell collisions but NOT 3+: the losers still RACED
// for home+1/home+2 via an atomicCompSwap in commit (inspector caught a static-camera coarse cell
// flip-flopping 18236<->18237 every frame). Under temporal amortization that slot-churn is fatal:
// a colliding cell bounces between two storage slots, each holding a different 1/N-amortized partial
// state, so its radiance flickers and propagates up the merge continuation onto the floor.
//
//   CLAIM (phase 0, run RC_ADD_CLAIM_SWEEPS times, barrier between): each cell skips past every
//     STRICTLY-smaller hash already in its probe chain, then atomicMin(h) at the first slot that is
//     EMPTY or holds a >= h value. atomicMin is commutative, so each sweep's result is independent
//     of thread order; repeated sweeps converge to hash-SORTED placement (lowest hash at home, next
//     at home+1, ...). A fixed cell set therefore yields identical slots every frame regardless of
//     how many cells collide — killing the overflow race for 3+ collisions too. p is monotonically
//     non-decreasing across sweeps (atomicMin only lowers slots, so "smaller ahead of me" only
//     grows), so it converges in <= chain-length sweeps; clusters longer than the sweep budget are
//     dropped that frame (astronomically rare at load factor 0.5).
//   COMMIT (phase 1): the claim already placed every cell, so commit just scans the chain to the
//     slot where buckets.x == h and publishes there (no .x CAS, no race). A barrier between claim
//     and commit guarantees every atomicMin has landed before commit reads buckets.x.
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

// CLAIM (one sweep) — skip every strictly-smaller hash already in the chain, then atomicMin at the
// first EMPTY-or->=h slot. Run RC_ADD_CLAIM_SWEEPS times (barrier between) → hash-sorted, order-
// independent placement. Idempotent: thousands of pixels emit the same cell and all land the same.
void claim(uint c, ivec3 cell) {
    CascadeDesc cd = cascades[c];
    ivec4 key  = ivec4(cell, int(c));
    uint  h    = hash_ivec4(key); if (h == EMPTY) h = 1u;
    uint  base = cd.bucket_off;
    uint  home = h % cd.bucket_cap;                              // local home index
    uint  p    = MAX_LINEAR;                                     // = no insertion point found (chain saturated)
    for (uint q = 0u; q < MAX_LINEAR; ++q) {
        uint v = buckets[base + ((home + q) % cd.bucket_cap)].x;
        if (v == EMPTY || v >= h) { p = q; break; }             // open or a >=h hash → my insertion point
        // v < h: a strictly-smaller hash sits ahead of me → step past it
    }
    if (p < MAX_LINEAR) atomicMin(buckets[base + ((home + p) % cd.bucket_cap)].x, h);
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

// COMMIT — the claim sweeps already placed every cell at the slot where buckets.x == h. Scan the
// chain to that slot and publish; stop at EMPTY (cluster exceeded the sweep budget → dropped). No
// .x CAS here, so there is no overflow race. (idx == slot because probe_off == bucket_off == base.)
void commit(uint c, ivec3 cell, vec3 center) {
    CascadeDesc cd = cascades[c];
    ivec4 key  = ivec4(cell, int(c));
    uint  h    = hash_ivec4(key); if (h == EMPTY) h = 1u;
    uint  base = cd.bucket_off;
    uint  home = h % cd.bucket_cap;
    for (uint p = 0u; p < MAX_LINEAR; ++p) {
        uint slot = base + ((home + p) % cd.bucket_cap);
        uint cur  = buckets[slot].x;
        if (cur == EMPTY) return;                      // chain ended before my slot → dropped this frame
        if (cur == h) {                                // my sorted slot (or a same-hash sibling pixel)
            uint existing = buckets[slot].y;
            if (existing == INVALID) { commit_data(c, cd, slot, slot, h, key, center); return; }
            if (probe_keys[existing] == key) return;   // already committed by a sibling pixel of this cell
            // same hash, DIFFERENT key (rare full-32-bit collision): no slot was reserved for it →
            // keep scanning; it hits EMPTY and drops this frame (deterministic; vanishingly rare).
        }
        // occupied by a different hash → next slot
    }
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
