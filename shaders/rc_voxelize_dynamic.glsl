#[compute]
#version 450

// Dynamic occluder voxelizer — one thread per PROXY triangle (capsule/box low-poly
// stand-ins for animated objects, posed CPU-side into world space each frame). Writes
// occupancy = 1 into a single-channel grid; emission/albedo are irrelevant because
// dynamic objects here are pure occluders (emissive dynamics route through Phase 4).
//
// Occupancy is a constant 1, so last-write-wins is order-independent → no determinism
// pass needed for this phase. The grid is cleared to 0 each frame before this runs.

layout(local_size_x = 64) in;

layout(set = 0, binding = 0, r8) uniform image3D occ_out;     // dynamic occupancy

struct Tri { vec4 v0; vec4 v1; vec4 v2; };                    // positions only; .w unused
layout(set = 0, binding = 1, std430) readonly buffer Tris { Tri tris[]; };

layout(push_constant) uniform PC {
    vec3 vox_origin; uint res;
    vec3 vox_extent; uint tri_count;
} pc;

bool sat_fail(float pa, float pb, float rad) { return min(pa, pb) > rad || max(pa, pb) < -rad; }

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

bool tri_box_overlap(vec3 boxcenter, vec3 bh, vec3 a, vec3 b, vec3 c) {
    vec3 v0 = a - boxcenter, v1 = b - boxcenter, v2 = c - boxcenter;
    vec3 e0 = v1 - v0, e1 = v2 - v1, e2 = v0 - v2;
    float fx, fy, fz;
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
    if (min(v0.x, min(v1.x, v2.x)) > bh.x || max(v0.x, max(v1.x, v2.x)) < -bh.x) return false;
    if (min(v0.y, min(v1.y, v2.y)) > bh.y || max(v0.y, max(v1.y, v2.y)) < -bh.y) return false;
    if (min(v0.z, min(v1.z, v2.z)) > bh.z || max(v0.z, max(v1.z, v2.z)) < -bh.z) return false;
    return plane_box_overlap(cross(e0, e1), v0, bh);
}

void main() {
    uint tid = gl_GlobalInvocationID.x;
    if (tid >= pc.tri_count) return;

    Tri t = tris[tid];
    vec3 a = t.v0.xyz, b = t.v1.xyz, c = t.v2.xyz;

    vec3  vsize = pc.vox_extent / float(pc.res);
    vec3  bh    = vsize * 0.5;
    vec3  tmin  = min(a, min(b, c));
    vec3  tmax  = max(a, max(b, c));
    ivec3 cmin  = clamp(ivec3(floor((tmin - pc.vox_origin) / vsize)), ivec3(0), ivec3(int(pc.res) - 1));
    ivec3 cmax  = clamp(ivec3(floor((tmax - pc.vox_origin) / vsize)), ivec3(0), ivec3(int(pc.res) - 1));

    for (int z = cmin.z; z <= cmax.z; ++z)
    for (int y = cmin.y; y <= cmax.y; ++y)
    for (int x = cmin.x; x <= cmax.x; ++x) {
        vec3 bc = pc.vox_origin + (vec3(x, y, z) + 0.5) * vsize;
        if (tri_box_overlap(bc, bh, a, b, c))
            imageStore(occ_out, ivec3(x, y, z), vec4(1.0));
    }
}
