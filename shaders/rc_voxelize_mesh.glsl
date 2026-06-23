#[compute]
#version 450

// Mesh voxelizer — one thread per triangle. Walks the voxels in the triangle's
// AABB and writes (emission.rgb, occupancy) wherever the triangle actually overlaps
// the voxel box (exact 13-axis separating-axis test, so thin/angled geometry doesn't
// leak or drop). This is the GEOMETRY trace target: persistent, off-screen-correct,
// no screen-space, no temporal. Run on bake (BAKE_ONCE) or when geometry changes.
//
// Big triangles must be pre-subdivided CPU-side so each spans only a few voxels,
// otherwise a single thread loops over a huge AABB and stalls the GPU.

layout(local_size_x = 64) in;

layout(set = 0, binding = 0, rgba16f) uniform image3D voxel_out;    // radiance: emission.rgb + occupancy.a
layout(set = 0, binding = 2, rgba8)   uniform image3D albedo_out;   // surface albedo.rgb
layout(set = 0, binding = 3, rgba8)   uniform image3D normal_out;   // face normal, *0.5+0.5
layout(set = 0, binding = 4, rgba16f) uniform image3D emission_out;

struct Tri { vec4 v0; vec4 v1; vec4 v2; vec4 emission; vec4 albedo; };  // .w of v* unused
layout(set = 0, binding = 1, std430) readonly buffer Tris { Tri tris[]; };

layout(push_constant) uniform PC {
    vec3 vox_extent; uint res;        // vox_origin no longer needed (cells are world-voxel % res)
    ivec3 slab_lo;   uint tri_count;  // clamp writes to [slab_lo, slab_lo+slab_dim)
    ivec3 slab_dim;  uint _p;
} pc;

bool sat_fail(float pa, float pb, float rad) {
    return min(pa, pb) > rad || max(pa, pb) < -rad;
}

bool plane_box_overlap(vec3 n, vec3 vert, vec3 maxbox) {
    vec3 vmin, vmax;
    for (int q = 0; q < 3; ++q) {
        if (n[q] > 0.0) { vmin[q] = -maxbox[q] - vert[q]; vmax[q] =  maxbox[q] - vert[q]; }
        else            { vmin[q] =  maxbox[q] - vert[q]; vmax[q] = -maxbox[q] - vert[q]; }
    }
    if (dot(n, vmin) > 0.0)  return false;
    if (dot(n, vmax) >= 0.0) return true;
    return false;
}

// Akenine-Möller triangle/box overlap. boxcenter/boxhalf in world space.
bool tri_box_overlap(vec3 boxcenter, vec3 bh, vec3 a, vec3 b, vec3 c) {
    vec3 v0 = a - boxcenter, v1 = b - boxcenter, v2 = c - boxcenter;
    vec3 e0 = v1 - v0, e1 = v2 - v1, e2 = v0 - v2;
    float fx, fy, fz;

    // 9 edge-cross-product axes
    fx = abs(e0.x); fy = abs(e0.y); fz = abs(e0.z);
    if (sat_fail(e0.z*v0.y - e0.y*v0.z, e0.z*v2.y - e0.y*v2.z, fz*bh.y + fy*bh.z)) return false;
    if (sat_fail(-e0.z*v0.x + e0.x*v0.z, -e0.z*v2.x + e0.x*v2.z, fz*bh.x + fx*bh.z)) return false;
    if (sat_fail(e0.y*v1.x - e0.x*v1.y, e0.y*v2.x - e0.x*v2.y, fy*bh.x + fx*bh.y)) return false;

    fx = abs(e1.x); fy = abs(e1.y); fz = abs(e1.z);
    if (sat_fail(e1.z*v0.y - e1.y*v0.z, e1.z*v2.y - e1.y*v2.z, fz*bh.y + fy*bh.z)) return false;
    if (sat_fail(-e1.z*v0.x + e1.x*v0.z, -e1.z*v2.x + e1.x*v2.z, fz*bh.x + fx*bh.z)) return false;
    if (sat_fail(e1.y*v0.x - e1.x*v0.y, e1.y*v1.x - e1.x*v1.y, fy*bh.x + fx*bh.y)) return false;

    fx = abs(e2.x); fy = abs(e2.y); fz = abs(e2.z);
    if (sat_fail(e2.z*v0.y - e2.y*v0.z, e2.z*v1.y - e2.y*v1.z, fz*bh.y + fy*bh.z)) return false;
    if (sat_fail(-e2.z*v0.x + e2.x*v0.z, -e2.z*v1.x + e2.x*v1.z, fz*bh.x + fx*bh.z)) return false;
    if (sat_fail(e2.y*v1.x - e2.x*v1.y, e2.y*v2.x - e2.x*v2.y, fy*bh.x + fx*bh.y)) return false;

    // 3 box-face axes (triangle AABB vs box)
    if (min(v0.x, min(v1.x, v2.x)) > bh.x || max(v0.x, max(v1.x, v2.x)) < -bh.x) return false;
    if (min(v0.y, min(v1.y, v2.y)) > bh.y || max(v0.y, max(v1.y, v2.y)) < -bh.y) return false;
    if (min(v0.z, min(v1.z, v2.z)) > bh.z || max(v0.z, max(v1.z, v2.z)) < -bh.z) return false;

    // triangle plane axis
    return plane_box_overlap(cross(e0, e1), v0, bh);
}

void main() {
    uint tid = gl_GlobalInvocationID.x;
    if (tid >= pc.tri_count) return;

    Tri t = tris[tid];
    vec3 a = t.v0.xyz, b = t.v1.xyz, c = t.v2.xyz;
    vec3 fn = normalize(cross(b - a, c - a));

    vec3  vsize = pc.vox_extent / float(pc.res);
    vec3  bh    = vsize * 0.5;
    vec3  tmin  = min(a, min(b, c));
    vec3  tmax  = max(a, max(b, c));

    // world-voxel AABB of the triangle, clamped to this slab's world-voxel range
    ivec3 slo = pc.slab_lo;
    ivec3 shi = pc.slab_lo + pc.slab_dim - ivec3(1);
    ivec3 wmin = max(ivec3(floor(tmin / vsize)), slo);
    ivec3 wmax = min(ivec3(floor(tmax / vsize)), shi);

    int R = int(pc.res);
    for (int z = wmin.z; z <= wmax.z; ++z)
    for (int y = wmin.y; y <= wmax.y; ++y)
    for (int x = wmin.x; x <= wmax.x; ++x) {
        vec3 bc = (vec3(x, y, z) + 0.5) * vsize;                 // absolute world centre
        if (tri_box_overlap(bc, bh, a, b, c)) {
            ivec3 cell = ((ivec3(x, y, z) % R) + R) % R;          // toroidal
            imageStore(emission_out, cell, vec4(t.emission.rgb, 1.0));
            imageStore(voxel_out,    cell, vec4(0.0, 0.0, 0.0, 1.0));
            imageStore(albedo_out,   cell, vec4(t.albedo.rgb,    1.0));
            imageStore(normal_out,   cell, vec4(fn * 0.5 + 0.5,  1.0));
        }
    }
}
