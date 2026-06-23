#[compute]
#version 450
#include "rc_trace.glslinc"
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
layout(set = 0, binding = 6, std430) writeonly buffer ProbeRad  { uvec2 probe_radiance[]; };

struct CascadeDesc {
    float spacing; float t_start; float t_end; float aperture;
    uint  dirs;    uint  oct_res; uint  bucket_off; uint  bucket_cap;
    uint  probe_off; uint probe_cap; uint rad_off; uint _p0;
};
layout(set = 0, binding = 7, std430) readonly buffer Cascades { CascadeDesc cascades[]; };

layout(push_constant) uniform PC { uint cascade; uint local_trans; uint _p1, _p2; } pc;

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

    uint slot_local = live_list[cd.probe_off + i];       // compact → actual slot
    uint idx        = cd.probe_off + slot_local;
    vec3 origin     = probe_world[idx].xyz;

    vec2 e   = (vec2(float(d % cd.oct_res), float(d / cd.oct_res)) + 0.5) / float(cd.oct_res);
    vec3 dir = oct_to_dir(e);

    vec4 r = rc_trace(origin, dir, cd.aperture, cd.t_start, cd.t_end, pc.local_trans);
    probe_radiance[cd.rad_off + slot_local * cd.dirs + d] =
        uvec2(packHalf2x16(r.rg), packHalf2x16(vec2(r.b, r.a)));
}
