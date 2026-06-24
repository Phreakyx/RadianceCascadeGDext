#[compute]
#version 450
#include "rc_trace.glslinc"
#include "rc_radiance_pack.glslinc"
// rc_trace(origin, dir, aperture_tan, t_start, t_end) + set 2 backend

// Sparse-RC (cascaded, NON-SHARED) — TRACE pass. Dispatched ONCE PER CASCADE (PC.cascade),
// indirect-sized to bucket_cap_c * dirs_c. One thread per (slot, direction): slots are the
// stable probe identity, so we iterate the whole hash region and skip empty slots. A live
// slot traces this cascade's interval and writes into its OWN deterministic radiance region.

layout(local_size_x = 64) in;

layout(set = 0, binding = 0, std430) readonly  buffer Buckets   { uvec2 buckets[]; };
layout(set = 0, binding = 1, std430) readonly buffer Alloc    { uint alloc_count[]; };
layout(set = 0, binding = 4, std430) readonly buffer LiveList { uint live_list[]; };
layout(set = 0, binding = 3, std430) readonly  buffer ProbeData { vec4  probe_world[]; };   // xyz center, w cascade
layout(set = 0, binding = 6, std430) writeonly buffer ProbeRad { uint probe_radiance[]; };   // packed rgba; merge folds the continuation (top cascade: final)

struct CascadeDesc {
    float spacing; float t_start; float t_end; float aperture;
    uint  dirs;    uint  oct_res; uint  bucket_off; uint  bucket_cap;
    uint  probe_off; uint probe_cap; uint rad_off; uint _p0;
};
layout(set = 0, binding = 7, std430) readonly buffer Cascades { CascadeDesc cascades[]; };

layout(push_constant) uniform PC { uint cascade; uint local_trans; uint frame; uint amortize_n; } pc;

const uint EMPTY = 0xffffffffu, INVALID = 0xffffffffu;

vec3 oct_to_dir(vec2 e) {
    e = e * 2.0 - 1.0;
    vec3 v = vec3(e.x, e.y, 1.0 - abs(e.x) - abs(e.y));
    if (v.z < 0.0) v.xy = (1.0 - abs(v.yx)) * sign(v.xy);
    return normalize(v);
}

void main() {
    CascadeDesc cd = cascades[pc.cascade];
    uint gid = gl_GlobalInvocationID.x;
    uint i   = gid / cd.dirs;                            // i-th LIVE probe (compact)
    uint d   = gid % cd.dirs;
    if (i >= alloc_count[pc.cascade]) return;            // past live list (dispatch rounding)

    uint entry      = live_list[cd.probe_off + i];       // compact → actual slot (+ bootstrap flag in bit 31)
    uint slot_local = entry & 0x7fffffffu;
    bool bootstrap  = (entry & 0x80000000u) != 0u;       // owner changed this frame → must refresh ALL dirs
    // Temporal amortization: a surviving probe re-traces only the rotating 1/N subset of its directions this
    // frame; the rest keep last frame's (same-cell) radiance, which the merge/gather still read. amortize_n=1
    // ⇒ every direction every frame (off; no behavioural change). Pure-ALU decision, no extra memory read.
    bool in_subset  = (pc.amortize_n <= 1u) || ((d % pc.amortize_n) == (pc.frame % pc.amortize_n));
    if (!bootstrap && !in_subset) return;                // keep this direction's persisted radiance

    uint idx        = cd.probe_off + slot_local;
    vec3 origin     = probe_world[idx].xyz;

    vec2 e   = (vec2(float(d % cd.oct_res), float(d / cd.oct_res)) + 0.5) / float(cd.oct_res);
    vec3 dir = oct_to_dir(e);

    vec4 r = rc_trace(origin, dir, cd.aperture, cd.t_start, cd.t_end, pc.local_trans);

    // Write the RAW interval straight to probe_radiance. The merge then folds this cascade's
    // continuation in place (it reads `it` from here for the directions retraced this frame, gated in
    // lockstep with trace so kept directions keep last frame's already-merged value). The TOP cascade
    // has no merge, so this raw value IS its final radiance. Persistent buckets give each probe a stable
    // slot, so amortization no longer needs the old temporal-EMA running-average (that buffer is gone).
    uint  ridx   = cd.rad_off + slot_local * cd.dirs + d;
    probe_radiance[ridx] = rc_pack_radiance(r);
}
