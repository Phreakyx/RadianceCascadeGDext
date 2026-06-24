#[compute]
#version 450

// Sparse-RC (cascaded, NON-SHARED) — ANGULAR PRE-REDUCE. Run ONCE per cascade-(c+1) probe,
// BEFORE merging c+1 into c, ONLY when r = cn.oct_res/cd.oct_res > 1 (the c1<-c2, c3<-c4 folds).
// Averages each c+1 probe's r² sub-directions down to cascade-c's oct resolution into a scratch
// buffer, so the MERGE fold becomes a plain 8-tap trilinear instead of 8 × r² samples per dir.
// Mathematically identical to the in-merge r² average — it's just hoisted out of the per-probe
// loop and amortized across the 8 coarse probes that share each c+1 neighbour.
//   reduced[slot_local · cd.dirs + rd] = (1/r²) · Σ_{r² sub-dirs} c+1.radiance

layout(local_size_x = 64) in;

layout(set = 0, binding = 6,  std430) readonly  buffer ProbeRad { uvec2 probe_radiance[]; };
layout(set = 0, binding = 8,  std430) writeonly buffer Reduced  { uvec2 reduced[]; };   // scratch, indexed by id_local
layout(set = 0, binding = 10, std430) readonly  buffer LastSeen { uint  last_seen[]; }; // per dense id (0 = free)

struct CascadeDesc {
    float spacing; float t_start; float t_end; float aperture;
    uint  dirs;    uint  oct_res; uint  bucket_off; uint  bucket_cap;
    uint  probe_off; uint probe_cap; uint rad_off; uint _p0;
};
layout(set = 0, binding = 7, std430) readonly buffer Cascades { CascadeDesc cascades[]; };

layout(push_constant) uniform PC { uint cascade; uint _p0, _p1, _p2; } pc;   // target cascade c

vec4 samp(uint gidx, uint rad_off, uint probe_off, uint dirs, uint d) {
    uvec2 p = probe_radiance[rad_off + (gidx - probe_off) * dirs + d];
    vec2 rg = unpackHalf2x16(p.x), ba = unpackHalf2x16(p.y);
    return vec4(rg, ba.x, ba.y);
}

void main() {
    uint c = pc.cascade;
    CascadeDesc cd = cascades[c];
    CascadeDesc cn = cascades[c + 1u];
    uint  r    = max(cn.oct_res / cd.oct_res, 1u);   // 2 for the folds this pass runs on
    float rinv = 1.0 / float(r * r);

    uint gid      = gl_GlobalInvocationID.x;
    uint id_local = gid / cd.dirs;                   // c+1 DENSE id
    uint rd       = gid % cd.dirs;                   // reduced (coarse) direction index
    if (id_local >= cn.probe_cap) return;

    uint nid = cn.probe_off + id_local;              // c+1 global dense id
    if (last_seen[nid] == 0u) return;                // free id — merge never reads it

    uint x = rd % cd.oct_res, y = rd / cd.oct_res;

    vec4 acc = vec4(0.0);
    for (uint sj = 0u; sj < r; ++sj)
    for (uint si = 0u; si < r; ++si) {
        uint sx = r * x + si, sy = r * y + sj;       // sx,sy < cn.oct_res by construction
        acc += samp(nid, cn.rad_off, cn.probe_off, cn.dirs, sy * cn.oct_res + sx);
    }
    acc *= rinv;

    reduced[id_local * cd.dirs + rd] =
        uvec2(packHalf2x16(acc.rg), packHalf2x16(vec2(acc.b, acc.a)));
}
