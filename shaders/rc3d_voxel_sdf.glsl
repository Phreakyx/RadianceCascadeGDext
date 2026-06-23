#[compute]
#version 450
layout(local_size_x = 4, local_size_y = 4, local_size_z = 4) in;

layout(set = 0, binding = 0, rgba16f) uniform readonly  image3D seed_in;    // HALF: xyz=nearest occ (window-rel, half), w=valid
layout(set = 0, binding = 1, rgba16f) uniform writeonly image3D seed_out;   // HALF
layout(set = 0, binding = 2, rgba16f) uniform readonly  image3D voxel_in;   // FULL: .a = occupancy
layout(set = 0, binding = 3, r16f)    uniform writeonly image3D sdf_out;     // HALF: distance in FULL-res voxels

layout(push_constant) uniform PC {
    uint mode; int step; uint res; uint _p0;     // res = HALF res (_vox_res/2)
    ivec3 phase; uint _p1;                        // phase = HALF phase (_vox_phase/2)
} pc;

const uint M_INIT = 0u, M_FLOOD = 1u, M_FINAL = 2u;

ivec3 rel(ivec3 c) { int R = int(pc.res); return ((c - pc.phase) % R + R) % R; }   // half-res texel → window-relative

void main() {
    int R = int(pc.res);                          // HALF res
    ivec3 c = ivec3(gl_GlobalInvocationID);
    if (any(greaterThanEqual(c, ivec3(R)))) return;

    if (pc.mode == M_INIT) {
        // downsample full-res occupancy: half-voxel occupied if ANY of its 2x2x2 children is.
        // conservative (over-includes) → SDF underestimates clearance → safe (never over-skips).
        float occ = 0.0;
        ivec3 fc = c * 2;
        for (int z = 0; z < 2; ++z)
        for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 2; ++x)
            occ = max(occ, imageLoad(voxel_in, fc + ivec3(x, y, z)).a);
        imageStore(seed_out, c, (occ > 0.5) ? vec4(vec3(rel(c)), 1.0) : vec4(0.0));
        return;
    }
    if (pc.mode == M_FLOOD) {
        ivec3 rc = rel(c);
        vec4  best  = imageLoad(seed_in, c);
        float bestd = (best.w > 0.5) ? distance(vec3(rc), best.xyz) : 1e9;
        for (int z = -1; z <= 1; ++z)
        for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x) {
            if (x == 0 && y == 0 && z == 0) continue;
            ivec3 n = ((c + ivec3(x, y, z) * pc.step) % R + R) % R;     // toroidal wrap
            vec4 s = imageLoad(seed_in, n);
            if (s.w < 0.5) continue;
            float d = distance(vec3(rc), s.xyz);                        // window-relative, linear
            if (d < bestd) { bestd = d; best = s; }
        }
        imageStore(seed_out, c, best);
        return;
    }
    // M_FINAL — distance is in HALF-res voxels; ×2 → FULL-res voxel units so the trace's
    // `textureLod(rc_sdf, ...).r * voxel_size` stays correct with the full-res voxel_size.
    vec4 s = imageLoad(seed_in, c);
    float d = (s.w > 0.5) ? 2.0 * distance(vec3(rel(c)), s.xyz) : float(R) * 2.0;
    imageStore(sdf_out, c, vec4(d, 0.0, 0.0, 0.0));
}