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

// Publish a slot we just claimed (buckets[slot].x == h) for this cell, then mark it seen. idx == slot
// (probe_off == bucket_off). Siblings of the same cell write identical key/world (torn-safe).
void publish(uint c, CascadeDesc cd, uint slot, uint h, ivec4 key, vec3 center) {
    uint owner = buckets[slot].y;
    if (owner == INVALID) {                              // unpublished → write key/world, claim .y
        probe_keys[slot]  = key;
        probe_world[slot] = vec4(center, float(c));
        memoryBarrierBuffer();                           // key/world visible before .y is published
        atomicCompSwap(buckets[slot].y, INVALID, slot);
        owner = buckets[slot].y;
    }
    if (owner != INVALID && probe_keys[owner] == key) mark_seen(c, cd, slot, h);
    // else a different key (rare full-32-bit collision) won the slot → this pixel gives up
}

// Find the cell, or insert it if absent. CRITICAL: we scan for the existing key FIRST and only insert
// once we hit EMPTY (chain end = key truly absent). An existing cell is therefore never migrated by a
// tombstone that opened up earlier in its chain — that migration used to bootstrap the cell for one
// frame (the "1-frame light burst" during movement) and caused a transient atomic burst. New cells
// reuse the EARLIEST tombstone (keeps chains compact); only a genuinely-new cell ever does an atomic.
void touch_cell(uint c, ivec3 cell, vec3 center) {
    CascadeDesc cd = cascades[c];
    ivec4 key  = ivec4(cell, int(c));
    uint  h    = hash_ivec4(key); if (h >= TOMB) h = 1u;     // avoid EMPTY & TOMB sentinels
    uint  base = cd.bucket_off;
    uint  cap  = cd.bucket_cap;
    uint  home = h % cap;
    uint  free_p = MAX_LINEAR;                               // offset of earliest reusable (TOMB) slot; MAX = none
    for (uint p = 0u; p < MAX_LINEAR; ++p) {
        uint slot = base + ((home + p) % cap);
        uint cur  = buckets[slot].x;
        if (cur == h) {                                      // our hash — is it our key?
            uint owner = buckets[slot].y;
            if (owner != INVALID && probe_keys[owner] == key) { mark_seen(c, cd, slot, h); return; }  // FOUND
            // same hash, DIFFERENT key (rare full collision) → keep scanning
        }
        else if (cur == TOMB) {
            if (free_p == MAX_LINEAR) free_p = p;            // remember earliest reusable; keep scanning for the key
        }
        else if (cur == EMPTY) {                             // chain end → key is ABSENT → insert
            if (free_p != MAX_LINEAR) {                      // reuse the earliest tombstone (compact chains)
                uint fslot = base + ((home + free_p) % cap);
                uint pv = atomicCompSwap(buckets[fslot].x, TOMB, h);
                if (pv == TOMB || pv == h) { publish(c, cd, fslot, h, key, center); return; }
                free_p = MAX_LINEAR;                         // tomb taken meanwhile → fall through to this EMPTY
            }
            uint pv2 = atomicCompSwap(buckets[slot].x, EMPTY, h);
            if (pv2 == EMPTY || pv2 == h) { publish(c, cd, slot, h, key, center); return; }
            // EMPTY grabbed by a different hash this instant → keep probing past it
        }
        // else occupied by a different hash → next slot
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
        // Insert ONLY the nearest probe cell to this surface point (not all 8 trilinear corners) — 8x
        // fewer inserts, the big rc_add win. Coverage is collective: any cell that contains visible
        // surface is the nearest cell of SOME pixel, so a gather's 8 trilinear neighbours are filled in
        // by adjacent pixels. Cells with no surface in them stay absent (they'd contribute ~nothing and
        // are down-weighted by the gather's plane weight anyway). Persistent buckets cover edge gaps.
        ivec3 cell   = ivec3(floor(world / s));         // nearest cell; probes center at (cell+0.5)*s
        vec3  center = (vec3(cell) + 0.5) * s;
        touch_cell(c, cell, center);
    }
}
