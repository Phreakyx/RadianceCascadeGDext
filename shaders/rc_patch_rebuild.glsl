#[compute]
#version 450

// Sparse-RC (cascaded, NON-SHARED) — REBUILD pass (dense-pool design). Dispatched ONCE PER CASCADE,
// one thread per dense id in [0, pcap). The hashmap was just cleared; this repopulates it from the
// PERSISTENT dense probe pool so probe identity is the stable dense id, never the (transient) slot:
//   • free id (last_seen == 0)            → skip.
//   • alive but aged out (> evict_age)    → EVICT: push the id onto the free-list, mark it free.
//   • alive                               → INSERT (key → global id) into the cleared hashmap.
// After this + a barrier, the ADD pass finds existing probes here (a read) or allocates new ids; both
// trace/merge/gather read storage by the id stored in buckets.y. No tombstones, no per-slot eviction.

layout(local_size_x = 64) in;

layout(set = 0, binding = 0,  std430) coherent buffer Buckets    { uvec2 buckets[]; };
layout(set = 0, binding = 2,  std430) readonly buffer ProbeKeys  { ivec4 probe_keys[]; };
layout(set = 0, binding = 8,  std430) coherent buffer LastSeen   { uint  last_seen[]; };    // per dense id (0 = free)
layout(set = 0, binding = 11, std430) coherent buffer FreeList   { uint  free_ids[]; };     // recycled id_local per [probe_off+i]
layout(set = 0, binding = 12, std430) coherent buffer AllocState { uint  alloc_state[]; };  // [c*2]=free_top, [c*2+1]=next_id

struct CascadeDesc {
    float spacing; float t_start; float t_end; float aperture;
    uint  dirs;    uint  oct_res; uint  bucket_off; uint  bucket_cap;
    uint  probe_off; uint probe_cap; uint rad_off; uint _p0;
};
layout(set = 0, binding = 7, std430) readonly buffer Cascades { CascadeDesc cascades[]; };

layout(push_constant) uniform PC { uint frame; uint evict_age; uint cascade; uint _p; } pc;

const uint EMPTY = 0xffffffffu, INVALID = 0xffffffffu, MAX_LINEAR = 64u;

uint hash_ivec4(ivec4 k) {
    uint h = uint(k.x)*73856093u ^ uint(k.y)*19349663u ^ uint(k.z)*83492791u ^ uint(k.w)*2654435761u;
    h ^= h>>15; h *= 2246822519u; h ^= h>>13; return h;
}

void main() {
    CascadeDesc cd = cascades[pc.cascade];
    uint id_local = gl_GlobalInvocationID.x;
    if (id_local >= cd.probe_cap) return;
    uint gid = cd.probe_off + id_local;

    uint ls = last_seen[gid];
    if (ls == 0u) return;                                   // free id — nothing to do

    if ((pc.frame - ls) > pc.evict_age) {                   // aged out → evict to the free-list
        uint t = atomicAdd(alloc_state[pc.cascade * 2u + 0u], 1u);   // push (free_top++)
        free_ids[cd.probe_off + t] = id_local;
        last_seen[gid] = 0u;                                // now free (radiance kept; bootstraps on reuse)
        return;
    }

    // alive → insert (key → gid) into the freshly-cleared hashmap (linear probe, claim via CAS on .x)
    ivec4 key  = probe_keys[gid];
    uint  h    = hash_ivec4(key); if (h >= 0xfffffffeu) h = 1u;   // same clamp as add/gather/merge
    uint  base = cd.bucket_off, cap = cd.bucket_cap;
    uint  home = h % cap;
    for (uint p = 0u; p < MAX_LINEAR; ++p) {
        uint slot = base + ((home + p) % cap);
        if (atomicCompSwap(buckets[slot].x, EMPTY, h) == EMPTY) {  // claimed a free slot for our hash
            buckets[slot].y = gid;                                 // publish our dense id (sole writer of this slot)
            return;
        }
        // slot taken (same hash by another id, or a different hash) → keep probing for our own slot
    }
    // chain full (load too high) → this id misses the map this frame; harmless, re-inserted next frame
}
