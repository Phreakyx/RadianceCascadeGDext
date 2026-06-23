#[compute]
#version 450
layout(local_size_x = 4, local_size_y = 4, local_size_z = 4) in;

layout(set = 0, binding = 0, r8) uniform readonly image3D cur_occ;   // this frame's binary occupancy
layout(set = 0, binding = 1, r8) uniform           image3D acc_occ;  // persistent accumulator (in-place)

layout(push_constant) uniform PC { uint res; float decay; uint _p0, _p1; } pc;

void main() {
    ivec3 c = ivec3(gl_GlobalInvocationID);
    if (any(greaterThanEqual(c, ivec3(int(pc.res))))) return;
    float cur = imageLoad(cur_occ, c).r;          // 1 where the occluder is now
    float acc = imageLoad(acc_occ, c).r;          // last frame's smoothed value
    imageStore(acc_occ, c, vec4(max(cur, acc * pc.decay)));   // present → 1, vacated → fades
}