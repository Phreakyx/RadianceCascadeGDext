#[compute]
#version 450

// Anisotropic (occlusion-aware) voxel mip. Replaces the isotropic mean mip.
// Stores radiance+occlusion PER AXIS-SIGN so a cone approaching a wall's shadowed
// side samples the occluded face (dark) instead of a directionless average — this
// is what kills wall-self radiance bleed at the root (Godot GIProbe technique).
//
// Direction order: 0:+X 1:-X 2:+Y 3:-Y 4:+Z 5:-Z
// Each dst[i] = vec4(rgb radiance escaping toward axis i, a = occlusion looking along i).
//
// Two source modes via pc.src_is_aniso:
//   0  level 1: src is the REAL mip-0 grid (one sampler, directionless rgba16f .a=occ)
//   1  level ≥2: src is the six aniso textures of the finer level
//
// Front-to-back occlusion compositing along each axis: the near child-pair hides the
// far pair, so a solid front face occludes a lit interior behind it.

layout(local_size_x = 4, local_size_y = 4, local_size_z = 4) in;

// --- destination: six views of the level being written (binding 0..5) ---
layout(set = 0, binding = 0, rgba16f) uniform writeonly image3D dst_px;  // +X
layout(set = 0, binding = 1, rgba16f) uniform writeonly image3D dst_nx;  // -X
layout(set = 0, binding = 2, rgba16f) uniform writeonly image3D dst_py;  // +Y
layout(set = 0, binding = 3, rgba16f) uniform writeonly image3D dst_ny;  // -Y
layout(set = 0, binding = 4, rgba16f) uniform writeonly image3D dst_pz;  // +Z
layout(set = 0, binding = 5, rgba16f) uniform writeonly image3D dst_nz;  // -Z

// --- source: either the real grid (binding 6) OR six finer aniso levels (7..12) ---
layout(set = 0, binding = 6)  uniform sampler3D src_grid;   // mip-0 real grid (src_is_aniso==0)
layout(set = 0, binding = 7)  uniform sampler3D src_px;     // finer +X (src_is_aniso==1)
layout(set = 0, binding = 8)  uniform sampler3D src_nx;
layout(set = 0, binding = 9)  uniform sampler3D src_py;
layout(set = 0, binding = 10) uniform sampler3D src_ny;
layout(set = 0, binding = 11) uniform sampler3D src_pz;
layout(set = 0, binding = 12) uniform sampler3D src_nz;

layout(push_constant) uniform PC {
    uint dst_res;        // resolution of the level being written
    uint src_is_aniso;   // 0: read src_grid (real mip0)   1: read six aniso src
    uint _p1; uint _p2;
} pc;

// Fetch the 8 children for direction `axis` (0..5). For level-1 (real grid) all six
// directions read the same isotropic child (radiance, occ); directionality emerges as
// occluded children start hiding lit ones along specific axes at coarser levels.
void fetch_children(ivec3 b, uint axis, out vec4 c[8]) {
    for (int z = 0; z < 2; ++z)
    for (int y = 0; y < 2; ++y)
    for (int x = 0; x < 2; ++x) {
        ivec3 p = b + ivec3(x, y, z);
        int idx = z * 4 + y * 2 + x;
        if (pc.src_is_aniso == 0u) {
            c[idx] = texelFetch(src_grid, p, 0);                 // rgb + occ, same for all axes
        } else {
            switch (axis) {
                case 0u: c[idx] = texelFetch(src_px, p, 0); break;
                case 1u: c[idx] = texelFetch(src_nx, p, 0); break;
                case 2u: c[idx] = texelFetch(src_py, p, 0); break;
                case 3u: c[idx] = texelFetch(src_ny, p, 0); break;
                case 4u: c[idx] = texelFetch(src_pz, p, 0); break;
                default: c[idx] = texelFetch(src_nz, p, 0); break;
            }
        }
    }
}

// Composite a near 2×2 slab over a far 2×2 slab along one axis: front-to-back.
// near/far are the averaged (rgb,occ) of the four children in each slab.
vec4 over(vec4 near, vec4 far) {
    float a = near.a + far.a * (1.0 - near.a);          // occlusion: front hides back
    vec3  c = near.rgb * near.a + far.rgb * far.a * (1.0 - near.a);
    // store radiance pre-divided by the composited occlusion so the trace's `*s.a` is exact;
    // guard the divide
    c = (a > 1e-4) ? c / a : vec3(0.0);
    return vec4(c, a);
}

// Reduce 8 children to one coarse value for `axis`, compositing the two slabs that
// face along that axis front-to-back (near slab = the one the +axis ray meets first).
vec4 reduce_axis(ivec3 b, uint axis) {
    vec4 c[8];
    fetch_children(b, axis, c);

    // child index = z*4 + y*2 + x. Slabs along each axis:
    bool flip = (axis & 1u) == 1u;            // odd index = negative axis → reverse near/far
    uint ax   = axis >> 1u;                   // 0:X 1:Y 2:Z

    vec4 lo, hi;   // lo = low-coord slab (x/y/z = 0), hi = high-coord slab
    if (ax == 0u) {            // X: group by x bit
        lo = 0.25*(c[0]+c[2]+c[4]+c[6]);   // x=0
        hi = 0.25*(c[1]+c[3]+c[5]+c[7]);   // x=1
    } else if (ax == 1u) {     // Y: group by y bit
        lo = 0.25*(c[0]+c[1]+c[4]+c[5]);   // y=0
        hi = 0.25*(c[2]+c[3]+c[6]+c[7]);   // y=1
    } else {                   // Z: group by z bit
        lo = 0.25*(c[0]+c[1]+c[2]+c[3]);   // z=0
        hi = 0.25*(c[4]+c[5]+c[6]+c[7]);   // z=1
    }
    // +axis ray enters the low slab first (near=lo); -axis enters high first (near=hi)
    return flip ? over(hi, lo) : over(lo, hi);
}

void main() {
    ivec3 d = ivec3(gl_GlobalInvocationID);
    if (any(greaterThanEqual(d, ivec3(int(pc.dst_res))))) return;
    ivec3 b = d * 2;

    imageStore(dst_px, d, reduce_axis(b, 0u));
    imageStore(dst_nx, d, reduce_axis(b, 1u));
    imageStore(dst_py, d, reduce_axis(b, 2u));
    imageStore(dst_ny, d, reduce_axis(b, 3u));
    imageStore(dst_pz, d, reduce_axis(b, 4u));
    imageStore(dst_nz, d, reduce_axis(b, 5u));
}
