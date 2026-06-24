#[compute]
#version 450
#include "rc_radiance_pack.glslinc"

// Sparse-RC (cascaded, NON-SHARED) — LOOKUP / DEBUG.
// Find the pixel's probe in ONE selected cascade (pc.cascade) within that cascade's own
// hash region, then visualize it.
//   kind 0 = probe-id colour (red = genuine miss). RAW single nearest probe on purpose —
//            this view exists to show probe identity / find-misses, so it must NOT smooth.
//   kind 1 = radiance. Now uses the SAME 8-probe trilinear reconstruction as the gather,
//            so the debug view reads like a reconstructed field instead of the raw probe
//            lattice (the old single-nearest snap is what made this view look speckled).
// Step pc.cascade 0..N-1 to watch the angular density climb on the coarse cascades.

layout(local_size_x = 8, local_size_y = 8) in;

layout(set = 0, binding = 0, std430) readonly buffer Buckets   { uvec2 buckets[]; };
layout(set = 0, binding = 2, std430) readonly buffer ProbeKeys { ivec4 probe_keys[]; };
layout(set = 0, binding = 4, rgba16f) uniform writeonly image2D debug_out;
layout(set = 0, binding = 5, std140) uniform CameraData {
    mat4 inv_proj; mat4 inv_view; mat4 fwd_proj; mat4 fwd_view; vec2 jitter; vec2 _pad;
} cam;
layout(set = 0, binding = 6, std430) readonly buffer ProbeRad { uint probe_radiance[]; };

struct CascadeDesc {
    float spacing; float t_start; float t_end; float aperture;
    uint  dirs;    uint  oct_res; uint  bucket_off; uint  bucket_cap;
    uint  probe_off; uint probe_cap; uint rad_off; uint _p0;
};
layout(set = 0, binding = 7, std430) readonly buffer Cascades { CascadeDesc cascades[]; };

layout(set = 1, binding = 0) uniform sampler2D depth_input;
layout(set = 1, binding = 1) uniform sampler2D normal_input;   // unused, layout match

layout(push_constant) uniform PC {
    uint  screen_width, screen_height, debug_kind, cascade;
    float z_near, z_far, _p0, _p1;
    vec3  sky_color; float _p2;          // matches the gather's sky fallback
} pc;

const float PI = 3.14159265359;
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
uint find_in_region(ivec4 key, uint boff, uint bcap) {
    uint h = hash_ivec4(key); if (h >= 0xfffffffeu) h = 1u;   // avoid EMPTY(ffffffff) & TOMB(fffffffe)
    uint slot = boff + (h % bcap);
    for (uint p = 0u; p < MAX_LINEAR; ++p) {
        uvec2 b = buckets[slot];
        if (b.x == EMPTY) return INVALID;
        if (b.x == h && b.y != INVALID && probe_keys[b.y] == key) return b.y;
        slot = boff + ((slot - boff + 1u) % bcap);
    }
    return INVALID;
}
vec3 id_color(uint i) { return fract(vec3(float(i)*0.61803, float(i)*0.0072, float(i)*0.0331)) * 0.8 + 0.2; }

// The pieces below mirror the gather (rc_patch_gather) EXACTLY so kind 1 previews the real output.
vec3 oct_to_dir(vec2 e) {
    e = e * 2.0 - 1.0;
    vec3 v = vec3(e.x, e.y, 1.0 - abs(e.x) - abs(e.y));
    if (v.z < 0.0) v.xy = (1.0 - abs(v.yx)) * sign(v.xy);
    return normalize(v);
}
vec4 samp(uint gidx, uint rad_off, uint probe_off, uint dirs, uint d) {     // rgb radiance + a transmittance
    uint i = rad_off + (gidx - probe_off) * dirs + d;
    return rc_unpack_radiance(probe_radiance[i]);
}
vec3 fetch_normal_ws(vec2 uv) {
    vec3 nv = normalize(texture(normal_input, uv).xyz * 2.0 - 1.0);
    return normalize(mat3(cam.inv_view) * nv);                              // view-space normal → world
}

void main() {
    ivec2 px = ivec2(gl_GlobalInvocationID.xy);
    if (px.x >= int(pc.screen_width) || px.y >= int(pc.screen_height)) return;
    vec2 uv = (vec2(px) + 0.5) / vec2(pc.screen_width, pc.screen_height);
    float raw = texture(depth_input, uv).r;
    if (raw < 0.00001) { imageStore(debug_out, px, vec4(0.0)); return; }

    vec3 world = screen_to_world(vec2(px), linearize_depth(raw));
    CascadeDesc cd = cascades[pc.cascade];

    // kind 0 — probe-id / miss diagnostic. RAW single nearest probe (identity must not blur).
    if (pc.debug_kind == 0u) {
        ivec3 cell = ivec3(floor(world / cd.spacing));
        uint  idx  = find_in_region(ivec4(cell, int(pc.cascade)), cd.bucket_off, cd.bucket_cap);
        if (idx == INVALID) { imageStore(debug_out, px, vec4(1.0, 0.0, 0.0, 1.0)); return; }
        imageStore(debug_out, px, vec4(id_color(idx), 1.0));
        return;
    }

    // kind 1 — GATHER PREVIEW. Runs the gather's EXACT reconstruction (8-probe trilinear with the
    // plane/backface weight, then cosine-integrate the directions + sky through residual transparency)
    // on the SELECTED cascade. At cascade 0 this equals the live GI; coarser cascades show that
    // cascade's folded contribution. This is what the frame will actually produce — use kind 0 for the
    // raw single-nearest probe view instead. Mirrors rc_patch_gather so the two can't silently diverge.
    vec3  n  = fetch_normal_ws(uv);
    vec3  sp = world / cd.spacing - 0.5;     // probe centres at (cell+0.5)*spacing
    ivec3 b  = ivec3(floor(sp));
    vec3  f  = sp - vec3(b);
    uint  nidx[8]; float nw[8]; float wsum = 0.0;
    for (int o = 0; o < 8; ++o) {
        ivec3 off  = ivec3(o & 1, (o >> 1) & 1, (o >> 2) & 1);
        ivec3 cell = b + off;
        uint  id   = find_in_region(ivec4(cell, int(pc.cascade)), cd.bucket_off, cd.bucket_cap);
        float w    = ((off.x==1)?f.x:1.0-f.x) * ((off.y==1)?f.y:1.0-f.y) * ((off.z==1)?f.z:1.0-f.z);
        vec3  to_probe = (vec3(cell) + 0.5) * cd.spacing - world;          // plane (backface) weight
        float pdist    = length(to_probe);
        float facing   = (pdist > 1e-4) ? dot(n, to_probe / pdist) : 1.0;
        w *= step(0.0, facing) * (facing * 0.5 + 0.5);
        nidx[o] = id;
        nw[o]   = (id == INVALID) ? 0.0 : w;
        wsum   += nw[o];
    }
    if (wsum <= 0.0) { imageStore(debug_out, px, vec4(1.0, 0.0, 0.0, 1.0)); return; }   // no usable probe → red
    float inv = 1.0 / wsum;
    vec3  E   = vec3(0.0);
    float dw  = 4.0 * PI / float(cd.dirs);
    for (uint d = 0u; d < cd.dirs; ++d) {
        vec4 acc = vec4(0.0);
        for (int o = 0; o < 8; ++o)
            if (nw[o] > 0.0) acc += nw[o] * samp(nidx[o], cd.rad_off, cd.probe_off, cd.dirs, d);
        acc *= inv;
        vec2 e   = (vec2(float(d % cd.oct_res), float(d / cd.oct_res)) + 0.5) / float(cd.oct_res);
        vec3 dir = oct_to_dir(e);
        float cw = max(dot(n, dir), 0.0);
        if (cw <= 0.0) continue;
        vec3 sky = pc.sky_color * smoothstep(-0.2, 0.3, dir.y);
        E += (acc.rgb + acc.a * sky) * cw * dw;
    }
    imageStore(debug_out, px, vec4(max(E, vec3(0.0)), 1.0));
}
