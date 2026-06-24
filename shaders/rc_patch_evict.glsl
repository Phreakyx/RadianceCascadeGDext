#[compute]
#version 450

// Sparse-RC (cascaded, NON-SHARED) — EVICT pass (persistent buckets).
// Frees any occupied slot not seen for more than `evict_age` frames, bounding occupancy as the
// player explores. A freed slot becomes a TOMBSTONE, not EMPTY: it's reusable by a new insert but
// does NOT terminate a probe chain, so a live cell sitting past it stays findable (plain EMPTY here
// would orphan it). .y is set INVALID and rad_tag is LEFT ALONE, so when a DIFFERENT cell reuses the
// slot its owner-hash mismatch forces a full re-trace (bootstrap) — no phantom radiance survives from
// the slot's previous occupant / previous level location. Dispatched only on frames where the camera
// view or voxel origin actually moved (CPU-gated): a stationary view reveals nothing new to evict for.

layout(local_size_x = 256) in;

layout(set = 0, binding = 0, std430) coherent buffer Buckets  { uvec2 buckets[]; };
layout(set = 0, binding = 1, std430) readonly buffer LastSeen { uint  last_seen[]; };

layout(push_constant) uniform PC { uint frame; uint evict_age; uint total_buckets; uint _p; } pc;

const uint EMPTY = 0xffffffffu, INVALID = 0xffffffffu, TOMB = 0xfffffffeu;

void main() {
    uint i = gl_GlobalInvocationID.x;
    if (i >= pc.total_buckets) return;
    uint x = buckets[i].x;
    if (x == EMPTY || x == TOMB) return;               // already free
    if ((pc.frame - last_seen[i]) > pc.evict_age)      // unseen long enough → tombstone (rad_tag kept)
        buckets[i] = uvec2(TOMB, INVALID);
}
