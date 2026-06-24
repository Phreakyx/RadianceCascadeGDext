#[compute]
#version 450

// Sparse-RC (cascaded, NON-SHARED) — ADD pass. PERSISTENT slot-keyed probes (find-or-insert).
//
// The hash table is NOT rebuilt each frame. A cell, once inserted, KEEPS its slot (and its
// accumulated/amortized radiance) until the evict pass reclaims it for being unseen too long. This
// kills both failure modes of the old per-frame rebuild:
//   • slot churn  — a colliding cell used to land on a different slot frame-to-frame (thread-order
//                   dependent overflow placement), flickering its amortized radiance.
//   • existence churn — a cell could nondeterministically fail to re-insert some frames (dropping out
//                   of the table entirely), so a continuation it fed vanished and the floor flickered.
// With persistence, after one successful insert a re-seeded cell is simply FOUND every frame (a pure
// read), so its slot and radiance are rock-stable; only the very first insert races (harmlessly).
//
// Per seeded cell this frame we `touch_cell`: find it (or insert if absent), then stamp last_seen and
// append it to the live list exactly once. last_seen drives three things: (1) live-list dedup — the
// thousands of pixels that emit the same coarse cell must append it once; (2) the merge's staleness
// gate — consumers only read cells seen THIS frame, so a persistent-but-unseen cell (e.g. left behind
// as the toroidal grid scrolled) can never leak phantom radiance at the wrong location; (3) eviction.

layout(local_size_x = 8, local_size_y = 8) in;

layout(set = 0, binding = 0, std430) coherent buffer Buckets   { uvec2 buckets[]; };
layout(set = 0, binding = 1, std430) coherent buffer Alloc     { uint  alloc_count[]; };
layout(set = 0, binding = 2, std430) coherent buffer ProbeKeys { ivec4 probe_keys[]; };
layout(set = 0, binding = 3, std430) coherent buffer ProbeData { vec4  probe_world[]; };   // xyz center, w cascade
layout(set = 0, binding = 4, std430) writeonly buffer LiveList { uint live_list[]; };
layout(set = 0, binding = 8, std430) coherent  buffer RadTag   { uint rad_tag[]; };    // per-slot owner hash (persistent)
layout(set = 0, binding = 10, std430) coherent buffer LastSeen { uint last_seen[]; };  // per-slot last-seen frame (persistent)
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
    float z_near, z_far; uint frame, _p2;
} pc;

const uint EMPTY = 0xffffffffu, INVALID = 0xffffffffu, TOMB = 0xfffffffeu, MAX_LINEAR = 64u;
// TOMB marks an evicted slot: reusable by a new insert, but (unlike EMPTY) it does NOT terminate a
// probe chain, so a live cell sitting past an evicted neighbour stays findable. Readers skip it for
// free (their match needs .y != INVALID, and an evicted slot's .y is INVALID).

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

// Stamp the slot as seen this frame and, for the FIRST toucher only, append it to the live list with
// the bootstrap bit. atomicExchange on last_seen is the dedup: exactly one of the cell's many pixels
// gets prev != frame. A persistent cell still owned by this hash (rad_tag == h) does NOT bootstrap —
// it keeps its radiance and continues amortizing; a slot reused by a different cell (rad_tag != h,
// e.g. after eviction) bootstraps all dirs so no stale/wrong-location radiance survives.
void mark_seen(uint c, CascadeDesc cd, uint slot, uint h) {
    // FAST PATH (critical for perf): a coarse cell is touched by thousands of pixels; without this
    // plain read they'd ALL serialize on one atomic address (rc_add was ~half the frame). Almost every
    // pixel sees the slot already stamped this frame and returns with no atomic at all.
    if (last_seen[slot] == pc.frame) return;
    if (atomicExchange(last_seen[slot], pc.frame) == pc.frame) return;   // lost the first-toucher race
    uint boot   = (rad_tag[slot] != h) ? 0x80000000u : 0u;
    rad_tag[slot] = h;
    uint live_i = atomicAdd(alloc_count[c], 1u);
    live_list[cd.probe_off + live_i] = (slot - cd.bucket_off) | boot;
}

// Find the cell, or insert it into the first free slot in its probe chain. Persistent: an existing
// cell is just found (a read) and re-stamped; only a never-yet-inserted cell claims a slot (one-time
// race, harmless because the slot is permanent thereafter). idx == slot (probe_off == bucket_off).
void touch_cell(uint c, ivec3 cell, vec3 center) {
    CascadeDesc cd = cascades[c];
    ivec4 key  = ivec4(cell, int(c));
    uint  h    = hash_ivec4(key); if (h >= TOMB) h = 1u;     // avoid EMPTY & TOMB sentinels
    uint  base = cd.bucket_off;
    uint  home = h % cd.bucket_cap;
    for (uint p = 0u; p < MAX_LINEAR; ++p) {
        uint slot = base + ((home + p) % cd.bucket_cap);
        uint cur  = buckets[slot].x;
        if (cur == EMPTY || cur == TOMB) {                   // free or tombstoned → claim for a new insert
            uint prev = atomicCompSwap(buckets[slot].x, cur, h);
            if (prev != cur && prev != h) continue;          // lost to a DIFFERENT hash → keep probing
            cur = h;                                          // we (or a same-hash sibling) now own .x here
            // (if this cell also persists further down the chain past an evicted gap, find returns this
            //  earlier slot and the old one is orphaned → re-tombstoned by evict later; harmless.)
        }
        if (cur == h) {
            uint owner = buckets[slot].y;
            if (owner == INVALID) {                          // unpublished → write key/world, claim .y
                probe_keys[slot]  = key;
                probe_world[slot] = vec4(center, float(c));
                memoryBarrierBuffer();                       // key/world visible before .y is published
                atomicCompSwap(buckets[slot].y, INVALID, slot);
                owner = buckets[slot].y;
            }
            if (owner != INVALID && probe_keys[owner] == key) { mark_seen(c, cd, slot, h); return; }
            // same hash, DIFFERENT key (rare full-32-bit collision) → this isn't my slot, keep probing
        }
        // occupied by a different hash → next slot
    }
    // chain full (load too high near this home) → cell skipped this frame; bounded by eviction + load 0.5
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
            ivec3 cell   = b + ivec3(o & 1, (o >> 1) & 1, (o >> 2) & 1);
            vec3  center = (vec3(cell) + 0.5) * s;
            touch_cell(c, cell, center);
        }
    }
}
