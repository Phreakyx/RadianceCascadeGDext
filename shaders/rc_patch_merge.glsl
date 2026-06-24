#[compute]
#version 450
#include "rc_radiance_pack.glslinc"

// Sparse-RC (cascaded, NON-SHARED) — MERGE. Fold cascade c+1's already-merged result into
// this cascade's interval: spatial-trilinear across c+1's probes (2x spacing) and angular-
// average over the r² sub-directions of c+1 this coarser direction's cone contains, where
// r = cn.oct_res / cd.oct_res. With the FLATTENED table oct={4,4,8,8,16} r is 1 OR 2, NOT
// always 2 — the old hardcoded 2x/4-subdir fold read OOB on the c0<-c1 and c2<-c3 folds
// (sx up to 2*3+1=7 into a 4-wide oct grid) and injected garbage continuation into c0/c2.
//   merged_rad   = interval_rad + interval_trans . avg_{r^2}( continuation_rad )
//   merged_trans = interval_trans . avg_{r^2}( continuation_trans )   <- cumulative transparency to inf
// Slot-keyed: iterate the COMPACT live list (live_list[probe_off + i], i < alloc_count[c]),
// mirroring rc_patch_trace; storage index = slot, stable. (Was: one thread per hash slot over
// the whole region + skip empties — wasted ~bucket_cap threads, e.g. 2M for c0, vs the far
// smaller live count.) Buckets is still read for the c+1 neighbour hash lookups below.

layout(local_size_x = 64) in;

layout(set = 0, binding = 0, std430) readonly buffer Buckets   { uvec2 buckets[]; };       // c+1 neighbour lookups
layout(set = 0, binding = 1, std430) readonly buffer Alloc     { uint  alloc_count[]; };   // live probes per cascade
layout(set = 0, binding = 2, std430) readonly buffer ProbeKeys { ivec4 probe_keys[]; };
layout(set = 0, binding = 3, std430) readonly buffer ProbeData { vec4  probe_world[]; };
layout(set = 0, binding = 4, std430) readonly buffer LiveList  { uint  live_list[]; };      // compact slot list
layout(set = 0, binding = 6, std430) buffer ProbeRad { uint probe_radiance[]; };

struct CascadeDesc {
    float spacing; float t_start; float t_end; float aperture;
    uint  dirs;    uint  oct_res; uint  bucket_off; uint  bucket_cap;
    uint  probe_off; uint probe_cap; uint rad_off; uint _p0;
};
layout(set = 0, binding = 7, std430) readonly buffer Cascades { CascadeDesc cascades[]; };
layout(set = 0, binding = 8, std430) readonly buffer Reduced { uint reduced_in[]; };

layout(push_constant) uniform PC { uint cascade; uint frame; uint amortize_n; uint _p2; } pc;
// `frame`/`amortize_n` drive the lockstep amortization gate (same values trace uses this frame).

const uint EMPTY = 0xffffffffu, INVALID = 0xffffffffu, MAX_LINEAR = 64u;

uint hash_ivec4(ivec4 k) {
    uint h = uint(k.x)*73856093u ^ uint(k.y)*19349663u ^ uint(k.z)*83492791u ^ uint(k.w)*2654435761u;
    h ^= h>>15; h *= 2246822519u; h ^= h>>13; return h;
}
uint find_in_region(ivec4 key, uint boff, uint bcap) {
    uint h = hash_ivec4(key); if (h >= 0xfffffffeu) h = 1u;   // avoid EMPTY(ffffffff) & TOMB(fffffffe)
    uint slot = boff + (h % bcap);
    for (uint p = 0u; p < MAX_LINEAR; ++p) {
        uvec2 b = buckets[slot];
        if (b.x == EMPTY) return INVALID;
        // Dense pool: the hashmap was rebuilt this frame from the ALIVE probes only, and every probe is
        // keyed by its world cell, so a key match is always a live, correct-location probe — no gate needed.
        if (b.x == h && b.y != INVALID && probe_keys[b.y] == key) return b.y;
        slot = boff + ((slot - boff + 1u) % bcap);
    }
    return INVALID;
}
vec4 samp(uint gidx, uint rad_off, uint probe_off, uint dirs, uint d) {     // probe_radiance: this cascade's RAW (own slot) or c+1's MERGED continuation
    uint i = rad_off + (gidx - probe_off) * dirs + d;
    return rc_unpack_radiance(probe_radiance[i]);   // packed rgba (rgb radiance + transmittance)
}

void main() {
    uint c = pc.cascade;
    CascadeDesc cd = cascades[c];
    CascadeDesc cn = cascades[c + 1u];

    uint i = gl_GlobalInvocationID.x;                        // i-th LIVE probe (compact)
    if (i >= alloc_count[c]) return;                         // past live list (dispatch rounding)
    uint entry      = live_list[cd.probe_off + i];
    uint slot_local = entry & 0x7fffffffu;                   // compact → actual slot
    bool bootstrap  = (entry & 0x80000000u) != 0u;           // owner changed → trace refreshed ALL dirs this frame
    uint idx        = cd.probe_off + slot_local;
    vec3 W   = probe_world[idx].xyz;

    vec3  sp = W / cn.spacing - 0.5;
    ivec3 b  = ivec3(floor(sp));
    vec3  f  = sp - vec3(b);
    uint  nidx[8]; float nw[8]; float wsum = 0.0;
    for (int o = 0; o < 8; ++o) {
        ivec3 off  = ivec3(o & 1, (o >> 1) & 1, (o >> 2) & 1);
        ivec3 cell = b + off;
        uint  id   = find_in_region(ivec4(cell, int(c + 1u)), cn.bucket_off, cn.bucket_cap);
        float w    = ((off.x==1)?f.x:1.0-f.x) * ((off.y==1)?f.y:1.0-f.y) * ((off.z==1)?f.z:1.0-f.z);
        nidx[o] = id;
        nw[o]   = (id == INVALID) ? 0.0 : w;
        wsum   += nw[o];
    }
    float inv_wsum = (wsum > 0.0) ? 1.0 / wsum : 0.0;

    // Angular sub-mapping ratio between this cascade and the next. Flattened table makes
    // this 1 (equal oct_res) OR 2 (oct_res doubles) — never assume 2.
    uint r = max(cn.oct_res / cd.oct_res, 1u);          // 1 (direct) or 2 (pre-reduced)
    float rinv = 1.0 / float(r * r);                    // average over r² sub-directions

    for (uint dc = 0u; dc < cd.dirs; ++dc) {
        // Amortize merge in LOCKSTEP with trace (rc_patch_trace): only re-merge directions whose raw
        // interval was (re)traced this frame; skipped dirs keep last frame's MERGED value untouched.
        // probe_radiance is read AND written in place, so re-merging a kept (already-merged) direction
        // would re-add the far-field continuation every frame → emissive compounds and flickers. Bootstrap
        // probes had ALL dirs traced this frame, so they merge all. Must use the SAME frame/N as trace.
        if (!bootstrap && pc.amortize_n > 1u && (dc % pc.amortize_n) != (pc.frame % pc.amortize_n)) continue;
        vec4 cont = vec4(0.0);                          // missing coarse neighbour -> no continuation
        if (inv_wsum > 0.0) {
            cont = vec4(0.0);
            for (int o = 0; o < 8; ++o) {
                if (nw[o] <= 0.0) continue;
                vec4 cs;
                if (r > 1u) {                            // pre-reduced: one tap, dir == dc
                    uint ri = (nidx[o] - cn.probe_off) * cd.dirs + dc;
                    cs = rc_unpack_radiance(reduced_in[ri]);
                } else {                                 // r==1: c+1 dirs == c dirs, read directly
                    cs = samp(nidx[o], cn.rad_off, cn.probe_off, cn.dirs, dc);
                }
                cont += nw[o] * cs;
            }
            cont *= inv_wsum;                            // NO rinv — averaging done in the reduce pass
        }
        // `it` = this cascade's RAW interval at our own slot (trace wrote it this frame for retraced
        // dirs; kept dirs are skipped by the lockstep gate above, so we never re-read a merged value).
        // In-place read-then-write is safe: this slot is owned by exactly one thread, and the lockstep
        // gate stops a kept (already-merged) direction from being folded again (which would compound).
        vec4 it = samp(idx, cd.rad_off, cd.probe_off, cd.dirs, dc);
        vec3  merged_rad   = it.rgb + it.a * cont.rgb;
        float merged_trans = it.a * cont.a;
        uint widx = cd.rad_off + slot_local * cd.dirs + dc;
        probe_radiance[widx] = rc_pack_radiance(vec4(merged_rad, merged_trans));
    }
}
