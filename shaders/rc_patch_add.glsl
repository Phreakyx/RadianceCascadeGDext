#[compute]
#version 450

// Sparse-RC (cascaded, NON-SHARED) — CREATE pass.
// DETERMINISTIC slot-keyed probes (Sannikov-style): a probe's storage index IS its hash
// slot, so add/trace/merge/gather all address the same cell to the same slot every frame.
// No allocation counter as an index → no frame-to-frame reshuffle → stable radiance.
// alloc_count survives only as a live count for the debug overlay.
//
// CONTENTION NOTE: every pixel inserts 8 cells × N cascades. On coarse cascades a single
// cell is touched by thousands of pixels, so insertion is hammered by huge thread contention.
// find_or_create therefore READS the slot before any atomic: an already-created cell
// (cur == h, the overwhelmingly common case) returns after a plain coherent read — no atomic,
// no spin. atomicCompSwap fires ONLY when the slot still reads EMPTY, i.e. once per cell, by
// the first thread to reach it. This turns a serialized CAS+spin storm into parallel reads.

layout(local_size_x = 8, local_size_y = 8) in;

layout(set = 0, binding = 0, std430) coherent buffer Buckets   { uvec2 buckets[]; };
layout(set = 0, binding = 1, std430) coherent buffer Alloc     { uint  alloc_count[]; };
layout(set = 0, binding = 2, std430) coherent buffer ProbeKeys { ivec4 probe_keys[]; };
layout(set = 0, binding = 3, std430) coherent buffer ProbeData { vec4  probe_world[]; };   // xyz center, w cascade
layout(set = 0, binding = 4, std430) writeonly buffer LiveList { uint live_list[]; };
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
    float z_near, z_far, _p1, _p2;
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

void find_or_create(uint c, ivec3 cell, vec3 center) {
    CascadeDesc cd = cascades[c];
    ivec4 key  = ivec4(cell, int(c));
    uint  h    = hash_ivec4(key); if (h == EMPTY) h = 1u;
    uint  base = cd.bucket_off;
    uint  slot = base + (h % cd.bucket_cap);
    for (uint p = 0u; p < MAX_LINEAR; ++p) {
        uint cur = buckets[slot].x;                  // READ first — no atomic on the hot path
        if (cur == EMPTY) {
            cur = atomicCompSwap(buckets[slot].x, EMPTY, h);   // claim only an empty slot
            if (cur == EMPTY) {                                 // we created this cell
                uint local = slot - base;            // the SLOT is the identity — deterministic, stable
                uint idx   = cd.probe_off + local;   // probe_off is aligned to bucket_off in the table
                probe_keys[idx]  = key;
                probe_world[idx] = vec4(center, float(c));
                memoryBarrierBuffer();               // publish key/world before the index becomes visible
                buckets[slot].y  = idx;
                uint live_i = atomicAdd(alloc_count[c], 1u);   // live count AND compact index
                live_list[cd.probe_off + live_i] = local;      // append this slot to the dispatch list
                return;
            }
            // lost the race: cur now holds the winner's hash (h if same cell, else a collision)
        }
        if (cur == h) {
            uint idx = buckets[slot].y;              // may be INVALID for a few cycles mid-publish — do NOT spin
            if (idx == INVALID || probe_keys[idx] == key) return;   // ours (or mid-publish) → done
            // real hash collision (different key, same h) → fall through to linear probe
        }
        slot = base + ((slot - base + 1u) % cd.bucket_cap);
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
            ivec3 cell   = b + ivec3(o & 1, (o >> 1) & 1, (o >> 2) & 1);
            vec3  center = (vec3(cell) + 0.5) * s;
            find_or_create(c, cell, center);
        }
    }
}
