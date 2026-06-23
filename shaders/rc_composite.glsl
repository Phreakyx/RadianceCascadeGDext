#[compute]
#version 450

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

// ---- Bindings (set=0 static, set=1 per-frame) ----
// set=0 — static: irradiance (read), albedo (read, dummy for now)
layout(set = 0, binding = 0, rgba16f) uniform readonly  image2D irradiance_in;
layout(set = 0, binding = 1) uniform sampler2D scene_albedo;   // dummy white today
// set=1 — per-frame: scene color (read AND write)
// We read it via sampler binding 0, write it via image binding 1 — same texture,
// two views.
layout(set = 0, binding = 2, rgba16f) uniform readonly image2D debug_in;
layout(set = 1, binding = 0) uniform sampler2D scene_color_sample;
layout(set = 1, binding = 1, rgba16f) uniform image2D scene_color_write;

layout(push_constant) uniform PC {
    uint  screen_width;
    uint  screen_height;
    float gi_intensity;
    uint debug_mode;
} pc;

vec3 approximate_albedo(vec3 lit_color) {
    float lum = dot(lit_color, vec3(0.2126, 0.7152, 0.0722));
    if (lum < 0.001) return vec3(0.0);                         // unlit → no recoverable albedo
    vec3  chroma = lit_color / lum;                            // hue at luminance 1
    const float ESTIMATED_LIGHTING = 0.3;                      // assumed avg incident level — the scene knob
    float albedo_lum = clamp(lum / ESTIMATED_LIGHTING, 0.04, 0.95);
    return clamp(chroma * albedo_lum, vec3(0.0), vec3(1.0));   // cap: a surface can't reflect >100%
}

// ── COMPOSITE FUNCTION ──────────────────────────────────────────────────────
// SEAM: today additive (no albedo plumbed). When albedo arrives, change body
// to `scene + irradiance * albedo * intensity` — multiplicative diffuse bounce.
// Nothing else in the addon needs to change.
vec3 apply_gi(vec3 scene, vec3 irradiance, vec3 albedo) {
    // Additive form — GI adds light, doesn't tint by surface reflectance yet.
    // Multiply by intensity for the user-facing strength knob.
    // return scene + irradiance * pc.gi_intensity;

    // FUTURE (when albedo buffer exists):
    return scene + irradiance * albedo * pc.gi_intensity;
}

void main() {
    uvec2 px = gl_GlobalInvocationID.xy;
    if (px.x >= pc.screen_width || px.y >= pc.screen_height) return;

    vec2 uv = (vec2(px) + 0.5) / vec2(pc.screen_width, pc.screen_height);

    vec3 scene     = texture(scene_color_sample, uv).rgb;
    vec3 irradiance = imageLoad(irradiance_in, ivec2(px)).rgb;
    vec3 albedo = approximate_albedo(scene);

    vec3 result = vec3(0,0,0); //apply_gi(scene, irradiance, albedo);

    if      (pc.debug_mode == 1u) result = imageLoad(debug_in, ivec2(px)).rgb;   // views 1/2/4 → _debug_tex
    else if (pc.debug_mode == 2u) result = irradiance.rgb;                       // view 3 → raw gather
    else                          result = scene + irradiance.rgb * approximate_albedo(scene) * pc.gi_intensity;
    imageStore(scene_color_write, ivec2(px), vec4(result, 1.0));
}