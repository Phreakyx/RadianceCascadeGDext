#[compute]
#version 450

// Sparse-RC (cascaded, NON-SHARED) — GATHER / INTEGRATE.
// The merge has folded every cascade down into cascade 0, so each cascade-0 probe now holds
// the FULL radiance field (near interval + continuation) in its dirs0 directions, plus the
// cumulative transparency to infinity per direction. Per pixel: reconstruct world+normal,
// trilinear-blend the 8 surrounding cascade-0 probes, and cosine-integrate the directions.
// Sky enters as sky_color weighted by each direction's residual transparency.

layout(local_size_x = 8, local_size_y = 8) in;

layout(set = 0, binding = 0, std430) readonly buffer Buckets   { uvec2 buckets[]; };
layout(set = 0, binding = 2, std430) readonly buffer ProbeKeys { ivec4 probe_keys[]; };
layout(set = 0, binding = 4, rgba16f) uniform writeonly image2D irradiance_out;
layout(set = 0, binding = 5, std140) uniform CameraData {
    mat4 inv_proj; mat4 inv_view; mat4 fwd_proj; mat4 fwd_view; vec2 jitter; vec2 _pad;
} cam;
layout(set = 0, binding = 6, std430) readonly buffer ProbeRad { uvec2 probe_radiance[]; };

struct CascadeDesc {
    float spacing; float t_start; float t_end; float aperture;
    uint  dirs;    uint  oct_res; uint  bucket_off; uint  bucket_cap;
    uint  probe_off; uint probe_cap; uint rad_off; uint _p0;
};
layout(set = 0, binding = 7, std430) readonly buffer Cascades { CascadeDesc cascades[]; };
layout(set = 0, binding = 9, std430) buffer Inspect { uint inspect[]; };   // DEBUG: dominant-probe value dump at pc._p0,_p1 (0xffffffff = off)

layout(set = 1, binding = 0) uniform sampler2D depth_input;
layout(set = 1, binding = 1) uniform sampler2D normal_input;

layout(push_constant) uniform PC {
    uint  screen_width, screen_height, _p0, _p1;
    float z_near, z_far, _p2, _p3;
    vec3  sky_color; float _p4;
} pc;

const float PI = 3.14159265359;
const uint  EMPTY = 0xffffffffu, INVALID = 0xffffffffu, MAX_LINEAR = 64u;

float linearize_depth(float raw) { return (raw < 0.00001) ? pc.z_far : pc.z_near / raw; }
vec3 screen_to_world(vec2 sc, float lin) {
    vec2 ndc = (sc + 0.5) / vec2(pc.screen_width, pc.screen_height) * 2.0 - 1.0;
    ndc.y = -ndc.y;
    vec4 vh = cam.inv_proj * vec4(ndc, 1.0, 1.0);
    vec3 vd = vh.xyz / vh.w;
    return (cam.inv_view * vec4(vd * (lin / -vd.z), 1.0)).xyz;
}
vec3 fetch_normal_ws(vec2 uv) {
    vec3 nv = normalize(texture(normal_input, uv).xyz * 2.0 - 1.0);
    return normalize(mat3(cam.inv_view) * nv);
}
vec3 oct_to_dir(vec2 e) {
    e = e * 2.0 - 1.0;
    vec3 v = vec3(e.x, e.y, 1.0 - abs(e.x) - abs(e.y));
    if (v.z < 0.0) v.xy = (1.0 - abs(v.yx)) * sign(v.xy);
    return normalize(v);
}
uint hash_ivec4(ivec4 k) {
    uint h = uint(k.x)*73856093u ^ uint(k.y)*19349663u ^ uint(k.z)*83492791u ^ uint(k.w)*2654435761u;
    h ^= h>>15; h *= 2246822519u; h ^= h>>13; return h;
}
uint find_in_region(ivec4 key, uint boff, uint bcap) {
    uint h = hash_ivec4(key); if (h == EMPTY) h = 1u;
    uint slot = boff + (h % bcap);
    for (uint p = 0u; p < MAX_LINEAR; ++p) {
        uvec2 b = buckets[slot];
        if (b.x == EMPTY) return INVALID;
        if (b.x == h && b.y != INVALID && probe_keys[b.y] == key) return b.y;
        slot = boff + ((slot - boff + 1u) % bcap);
    }
    return INVALID;
}
vec4 samp(uint gidx, uint rad_off, uint probe_off, uint dirs, uint d) {
    uvec2 p = probe_radiance[rad_off + (gidx - probe_off) * dirs + d];
    vec2 rg = unpackHalf2x16(p.x), ba = unpackHalf2x16(p.y);
    return vec4(rg, ba.x, ba.y);   // rgb radiance + cumulative transparency
}

void main() {
    ivec2 px = ivec2(gl_GlobalInvocationID.xy);
    if (px.x >= int(pc.screen_width) || px.y >= int(pc.screen_height)) return;
    vec2 uv = (vec2(px) + 0.5) / vec2(pc.screen_width, pc.screen_height);
    float raw = texture(depth_input, uv).r;
    if (raw < 0.00001) { imageStore(irradiance_out, px, vec4(0.0)); return; }

    vec3 world = screen_to_world(vec2(px), linearize_depth(raw));
    vec3 n     = fetch_normal_ws(uv);

    CascadeDesc cd = cascades[0];
    vec3  sp = world / cd.spacing - 0.5;
    ivec3 b  = ivec3(floor(sp));
    vec3  f  = sp - vec3(b);
    uint  nidx[8]; float nw[8]; float wsum = 0.0;
    for (int o = 0; o < 8; ++o) {
        ivec3 off  = ivec3(o & 1, (o >> 1) & 1, (o >> 2) & 1);
        ivec3 cell = b + off;
        uint  id   = find_in_region(ivec4(cell, 0), cd.bucket_off, cd.bucket_cap);
        float w    = ((off.x==1)?f.x:1.0-f.x) * ((off.y==1)?f.y:1.0-f.y) * ((off.z==1)?f.z:1.0-f.z);
        // plane (backface) weight — reject probes behind the surface so a wall thinner than the
        // c0 spacing can't blend its front-lit probes onto a back-face pixel.
        vec3  to_probe = (vec3(cell) + 0.5) * cd.spacing - world;
        float pdist    = length(to_probe);
        float facing   = (pdist > 1e-4) ? dot(n, to_probe / pdist) : 1.0;   // ~1 front, <0 behind
        float vis      = facing * 0.5 + 0.5;                                // [0,1]
        w *= step(0.0, facing) * vis;                                         // sharpen → back probes ~0
        nidx[o] = id;
        nw[o]   = (id == INVALID) ? 0.0 : w;
        wsum   += nw[o];
    }
    float inv = (wsum > 0.0) ? 1.0 / wsum : 0.0;

    vec3  E  = vec3(0.0);
    float dw = 4.0 * PI / float(cd.dirs);                    // solid angle per direction (sphere)
    for (uint d = 0u; d < cd.dirs; ++d) {
        vec4 acc = vec4(0.0);
        if (inv > 0.0) {
            for (int o = 0; o < 8; ++o)
                if (nw[o] > 0.0) acc += nw[o] * samp(nidx[o], cd.rad_off, cd.probe_off, cd.dirs, d);
            acc *= inv;
        }
        vec2 e   = (vec2(float(d % cd.oct_res), float(d / cd.oct_res)) + 0.5) / float(cd.oct_res);
        vec3 dir = oct_to_dir(e);
        float cw = max(dot(n, dir), 0.0);
        if (cw <= 0.0) continue;
        vec3 sky = pc.sky_color * smoothstep(-0.2, 0.3, dir.y);
        vec3 L   = acc.rgb + acc.a * sky;                    // arriving radiance + sky through residual transparency
        E += L * cw * dw;
    }
	E = (any(isnan(E)) || any(isinf(E))) ? vec3(0.0) : max(E, vec3(0.0));
    // ---- DEBUG probe inspector: dump the dominant covering c0 probe at the target pixel ----
    if (pc._p0 != 0xffffffffu && uint(px.x) == pc._p0 && uint(px.y) == pc._p1) {
        int best_o = -1; float best_w = 0.0;
        for (int o = 0; o < 8; ++o) if (nw[o] > best_w) { best_w = nw[o]; best_o = o; }
        inspect[0] = 1u;                                       // a surface pixel was inspected this frame
        inspect[1] = (best_o >= 0) ? 1u : 0u;                  // a covering probe was found
        inspect[2] = floatBitsToUint(world.x);
        inspect[3] = floatBitsToUint(world.y);
        inspect[4] = floatBitsToUint(world.z);
        inspect[5] = floatBitsToUint(E.r);
        inspect[6] = floatBitsToUint(E.g);
        inspect[7] = floatBitsToUint(E.b);
        if (best_o >= 0) {
            uint  gi = nidx[best_o];
            ivec4 k  = probe_keys[gi];
            inspect[8]  = gi - cd.probe_off;                   // slot_local = probe identity
            inspect[9]  = uint(k.x); inspect[10] = uint(k.y); inspect[11] = uint(k.z); inspect[12] = uint(k.w);
            for (uint d = 0u; d < cd.dirs; ++d) {
                vec4 r = samp(gi, cd.rad_off, cd.probe_off, cd.dirs, d);   // MERGED radiance.rgb + transmittance.a
                inspect[16u + d*4u + 0u] = floatBitsToUint(r.r);
                inspect[16u + d*4u + 1u] = floatBitsToUint(r.g);
                inspect[16u + d*4u + 2u] = floatBitsToUint(r.b);
                inspect[16u + d*4u + 3u] = floatBitsToUint(r.a);
            }
        }
    }
    imageStore(irradiance_out, px, vec4(E, 1.0));
}
