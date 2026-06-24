#[compute]
#version 450

// Sparse-RC (cascaded, NON-SHARED) — ADD pass. DENSE-POOL find-or-allocate.
//
// The hashmap was cleared and the rebuild pass re-inserted every ALIVE probe (key → dense id). So a
// seeded cell is either FOUND here (a read → its stable dense id) or ABSENT (new this frame, or
// returned after eviction) → we ALLOCATE a fresh dense id (pop the free-list, else bump a counter) and
// insert it. Probe identity is the dense id, not the slot, so the hashmap can churn freely with no
// effect on radiance. Per seeded cell we mark_seen: stamp last_seen and append to the live list once.
//
// Concurrent insert of one new cell by its many pixels: the SINGLE thread that wins the slot's .x CAS
// allocates the id and publishes .y; siblings that see the slot mid-insert (.y == INVALID) just give up
// for the frame — the winner already seeded it, so there's no double-allocation and no duplicate.

layout(local_size_x = 8, local_size_y = 8) in;

layout(set = 0, binding = 0,  std430) coherent buffer Buckets    { uvec2 buckets[]; };
layout(set = 0, binding = 1,  std430) coherent buffer Alloc      { uint  alloc_count[]; };
layout(set = 0, binding = 2,  std430) coherent buffer ProbeKeys  { ivec4 probe_keys[]; };
layout(set = 0, binding = 3,  std430) coherent buffer ProbeData  { vec4  probe_world[]; };   // xyz center, w cascade
layout(set = 0, binding = 4,  std430) writeonly buffer LiveList  { uint  live_list[]; };
layout(set = 0, binding = 8,  std430) coherent buffer RadTag     { uint  rad_tag[]; };        // per dense id owner hash
layout(set = 0, binding = 10, std430) coherent buffer LastSeen   { uint  last_seen[]; };      // per dense id (0 = free)
layout(set = 0, binding = 11, std430) coherent buffer FreeList   { uint  free_ids[]; };       // recycled id_local
layout(set = 0, binding = 12, std430) coherent buffer AllocState { uint  alloc_state[]; };    // [c*2]=free_top,[c*2+1]=next_id
layout(set = 0, binding = 5,  std140) uniform CameraData {
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

// Allocate a fresh dense id for cascade c: pop the free-list (recycled), else bump the high-water
// counter. Returns a GLOBAL id (probe_off + local), or INVALID if the pool is full. Called only by the
// thread that won a slot's .x CAS — one allocation per new cell, so no per-pixel allocation storm.
uint alloc_id(uint c, CascadeDesc cd) {
    int t = int(atomicAdd(alloc_state[c * 2u + 0u], 0xffffffffu));   // free_top-- ; t = old value
    if (t > 0) return cd.probe_off + free_ids[cd.probe_off + uint(t - 1)];   // valid pop
    atomicAdd(alloc_state[c * 2u + 0u], 1u);                          // underflow → undo decrement
    uint b = atomicAdd(alloc_state[c * 2u + 1u], 1u);                 // bump next_id
    return (b < cd.probe_cap) ? (cd.probe_off + b) : INVALID;
}

// Stamp the dense id as seen this frame and, for the FIRST toucher only, append it to the live list with
// the bootstrap bit. Fast-path plain read avoids the per-pixel atomic on coarse cells (thousands of
// pixels per cell). rad_tag mismatch (a freed/reused id) bootstraps all dirs → no stale radiance.
void mark_seen(uint c, CascadeDesc cd, uint gid, uint h) {
    if (last_seen[gid] == pc.frame) return;
    if (atomicExchange(last_seen[gid], pc.frame) == pc.frame) return;
    uint boot   = (rad_tag[gid] != h) ? 0x80000000u : 0u;
    rad_tag[gid] = h;
    uint live_i = atomicAdd(alloc_count[c], 1u);
    live_list[cd.probe_off + live_i] = (gid - cd.probe_off) | boot;   // id_local + bootstrap bit
}

void touch_cell(uint c, ivec3 cell, vec3 center) {
    CascadeDesc cd = cascades[c];
    ivec4 key  = ivec4(cell, int(c));
    uint  h    = hash_ivec4(key); if (h >= 0xfffffffeu) h = 1u;   // same clamp as rebuild/gather/merge
    uint  base = cd.bucket_off, cap = cd.bucket_cap;
    uint  home = h % cap;
    for (uint p = 0u; p < MAX_LINEAR; ++p) {
        uint slot = base + ((home + p) % cap);
        uint cur  = buckets[slot].x;
        if (cur == EMPTY) {                                  // chain end → cell ABSENT → insert
            uint prev = atomicCompSwap(buckets[slot].x, EMPTY, h);
            if (prev == EMPTY) {                             // I won → allocate a dense id + publish
                uint gid = alloc_id(c, cd);
                if (gid == INVALID) return;                  // pool full → drop (slot gone next clear)
                probe_keys[gid]  = key;
                probe_world[gid] = vec4(center, float(c));
                memoryBarrierBuffer();                       // key/world visible before .y is published
                buckets[slot].y = gid;
                mark_seen(c, cd, gid, h);
                return;
            }
            if (prev != h) continue;                         // a different hash took it → keep probing
            cur = h;                                         // a same-hash sibling is inserting → handle below
        }
        if (cur == h) {
            uint gid = buckets[slot].y;
            if (gid == INVALID) return;                      // sibling mid-insert; it seeds the cell → give up
            if (probe_keys[gid] == key) { mark_seen(c, cd, gid, h); return; }
            // same hash, different key (rare full-32-bit collision) → keep probing
        }
        // occupied by a different hash → next slot
    }
    // chain full (load too high near this home) → cell skipped this frame
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
        // Insert the 8 cells the GATHER will trilinear-read for this surface point (gather bases at
        // floor(world/s - 0.5); match exactly so no neighbour is missing). Nearest-only undersamples at
        // grazing angles (dead probes on receding floors), so keep the 8-corner splat.
        vec3  sp = world / s - 0.5;
        ivec3 b  = ivec3(floor(sp));
        for (int o = 0; o < 8; ++o) {
            ivec3 cell   = b + ivec3(o & 1, (o >> 1) & 1, (o >> 2) & 1);
            vec3  center = (vec3(cell) + 0.5) * s;
            touch_cell(c, cell, center);
        }
    }
}
