#[compute]
#version 450
#define GATHER_SET 2
#include "rc_patch_gather_inc.glslinc"

// Sparse-RC — IRRADIANCE UPSAMPLE. Joint-bilateral upsample of the half-res gather output to
// full output resolution. Irradiance (diffuse bounce) is low-frequency, so the gather runs at
// half-res; this pass restores full-res detail using the FULL-res depth + normal as the edge-
// stopping guide, so light never bleeds across silhouettes / normal discontinuities.
// Bindings mirror the gather: set0 = static (half irradiance + full-res output image),
// set1 = per-frame (the same depth/normal you feed the gather).

layout(local_size_x = 8, local_size_y = 8) in;

layout(set = 0, binding = 0) uniform sampler2D half_irradiance;                  // gather output, HALF res
layout(set = 0, binding = 1, rgba16f) uniform writeonly image2D irradiance_out;  // FULL-res (composite reads this)

layout(set = 1, binding = 0) uniform sampler2D depth_input;                      // FULL-res depth (nearest)
layout(set = 1, binding = 1) uniform sampler2D normal_input;                     // FULL-res normal, *0.5+0.5

layout(push_constant) uniform PC {
    uint  full_w, full_h, half_w, half_h;
    float z_near, z_far, sigma_z, normal_pow;
    vec3  sky_color; float _pad;
} pc;

const float EDGE_LO = 0.20;   // bilateral wsum below this → full-res gather fully replaces it
const float EDGE_HI = 0.60;   // above this → pure bilateral (interior). Between = blended.

float lin(float raw) { return (raw < 0.00001) ? pc.z_far : pc.z_near / raw; }
vec3  dec_n(vec2 uv) { return normalize(texture(normal_input, uv).xyz * 2.0 - 1.0); }

void main() {
    ivec2 px = ivec2(gl_GlobalInvocationID.xy);
    if (px.x >= int(pc.full_w) || px.y >= int(pc.full_h)) return;

    vec2 uv = (vec2(px) + 0.5) / vec2(pc.full_w, pc.full_h);
    float dp = lin(texture(depth_input, uv).r);
    if (dp >= pc.z_far) { imageStore(irradiance_out, px, vec4(0.0)); return; }    // background
    vec3 np = dec_n(uv);
    
    vec2  hpix = uv * vec2(pc.half_w, pc.half_h) - 0.5;
    ivec2 hb   = ivec2(floor(hpix));
    vec2  hf   = hpix - vec2(hb);
    ivec2 hmax = ivec2(pc.half_w, pc.half_h) - 1;

    vec3  acc = vec3(0.0);
    float wsum = 0.0;
    vec3  best_irr = vec3(0.0);
    float best_w   = -1.0;

    for (int o = 0; o < 4; ++o) {
        ivec2 ht  = clamp(hb + ivec2(o & 1, (o >> 1) & 1), ivec2(0), hmax);
        vec2  huv = (vec2(ht) + 0.5) / vec2(pc.half_w, pc.half_h);
        float dh  = lin(texture(depth_input, huv).r);
        vec3  nh  = dec_n(huv);

        float bw = ((o & 1) == 1 ? hf.x : 1.0 - hf.x) * (((o >> 1) & 1) == 1 ? hf.y : 1.0 - hf.y);
        float wz = exp(-abs(dp - dh) / (pc.sigma_z * dp + 1e-3));
        float wn = pow(max(dot(np, nh), 0.0), pc.normal_pow);
        float w  = bw * wz * wn;

        vec3 irr = texelFetch(half_irradiance, ht, 0).rgb;
        acc  += w * irr;
        wsum += w;
        if (w > best_w) { best_w = w; best_irr = irr; }
    }

    vec3  bil  = (wsum > 1e-5) ? acc / wsum : best_irr;
    float edge = 1.0 - smoothstep(EDGE_LO, EDGE_HI, wsum);     // 1 at silhouettes, 0 in interior
    vec3  result = bil;
    if (edge > 0.001) {
        vec3 world = g_screen_to_world(vec2(px), dp, vec2(pc.full_w, pc.full_h));
        vec3 n_ws  = normalize(mat3(cam.inv_view) * np);       // np is view-space; gather wants world
        vec4 g     = gather_c0_irradiance(world, n_ws, pc.sky_color);
        if (g.a > 0.5) result = mix(bil, g.rgb, edge);         // probe found → blend in full-res GI
    }
    imageStore(irradiance_out, px, vec4(result, 1.0));
}
