#[compute]
#version 450
layout(local_size_x = 8, local_size_y = 8) in;

// Sparse-RC — IRRADIANCE À-TROUS. Edge-aware spatial smoothing of the half-res gather output,
// run BEFORE the upsample. Removes c0 angular quantization (16 dirs) + 0.25 m radiance-field
// structure while preserving silhouettes via depth + normal edge stops (same guides + constants
// as the upsample). Pure spatial filter, no temporal accumulation. Ping-pong: an EVEN pass count
// lands the result back in _irradiance_half so the upsample's static set0 reads it unchanged.

layout(set = 0, binding = 0) uniform sampler2D irr_in;                       // half-res irradiance (this pass's source)
layout(set = 0, binding = 1, rgba16f) uniform writeonly image2D irr_out;     // half-res (ping-pong target)

layout(set = 1, binding = 0) uniform sampler2D depth_input;                  // FULL-res depth  (nearest)
layout(set = 1, binding = 1) uniform sampler2D normal_input;                 // FULL-res normal, *0.5+0.5

layout(push_constant) uniform PC {
    uint  half_w, half_h; int step; uint _p0;
    float z_near, z_far, sigma_z, normal_pow;
} pc;

float lin(float raw) { return (raw < 0.00001) ? pc.z_far : pc.z_near / raw; }
vec3  dec_n(vec2 uv) { return normalize(texture(normal_input, uv).xyz * 2.0 - 1.0); }

const float K[3] = float[](0.375, 0.25, 0.0625);   // B3-spline à-trous weights, |offset| 0,1,2
const float SIGMA_L = 0.6;   // luminance range-stop width, RELATIVE to centre luminance (TODO: expose as tunable)
float luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }

void main() {
    ivec2 px = ivec2(gl_GlobalInvocationID.xy);
    if (px.x >= int(pc.half_w) || px.y >= int(pc.half_h)) return;

    vec2  uv    = (vec2(px) + 0.5) / vec2(pc.half_w, pc.half_h);
    vec3  irr_c = texelFetch(irr_in, px, 0).rgb;
    float dc    = lin(texture(depth_input, uv).r);
    if (dc >= pc.z_far) { imageStore(irr_out, px, vec4(irr_c, 1.0)); return; }   // background: passthrough
    vec3  nc    = dec_n(uv);

    ivec2 hmax = ivec2(pc.half_w, pc.half_h) - 1;
    float lc   = luma(irr_c);                                                   // centre luminance for the range stop
    vec3  acc  = vec3(0.0);
    float wsum = 0.0;
    for (int dy = -2; dy <= 2; ++dy)
    for (int dx = -2; dx <= 2; ++dx) {
        ivec2 t   = clamp(px + ivec2(dx, dy) * pc.step, ivec2(0), hmax);
        vec2  tuv = (vec2(t) + 0.5) / vec2(pc.half_w, pc.half_h);
        float dt  = lin(texture(depth_input, tuv).r);
        vec3  nt  = dec_n(tuv);
        vec3  it  = texelFetch(irr_in, t, 0).rgb;

        float kw = K[abs(dx)] * K[abs(dy)];                                     // spatial
        float wz = exp(-abs(dc - dt) / (pc.sigma_z * dc + 1e-3));               // depth stop
        float wn = pow(max(dot(nc, nt), 0.0), pc.normal_pow);                   // normal stop
        // RANGE (luminance) stop. Depth+normal are both flat across a floor, so without this the
        // kernel blurs the bright near-source red bounce freely across the whole floor (the wash in
        // view 4 that isn't in the pre-denoise view 3). Relative to the centre luma so it auto-scales
        // with exposure; a near-dark centre gives bright neighbours ~0 weight → red can't bleed into
        // the dark floor and the lit/dark edge survives the denoise.
        float wl = exp(-abs(lc - luma(it)) / (SIGMA_L * lc + 1e-3));            // range stop
        float w  = kw * wz * wn * wl;

        acc  += w * it;
        wsum += w;
    }
    vec3 outc = (wsum > 1e-5) ? acc / wsum : irr_c;
    imageStore(irr_out, px, vec4(outc, 1.0));
}