#pragma once
#include <algorithm>
#include <cmath>

struct cube_camera_pose_t { float yaw=0, pitch=0, roll=0, pullback=0, rise=0; };
// A bounded, closed camera arc: neutral pose and zero velocity at both ends.
inline cube_camera_pose_t cube_camera_sweep(float progress, int horizontal, int vertical)
{
    constexpr float pi=3.14159265358979323846f;
    float t=std::clamp(progress,0.0f,1.0f);
    if(t<=0 || t>=1) return {};
    float envelope=std::sin(pi*t); envelope*=envelope;
    float arc=std::sin(2*pi*t)*envelope;
    return {horizontal*1.05f*envelope + vertical*.30f*arc,
        vertical*.85f*envelope + horizontal*.32f*arc,
        -horizontal*.24f*arc + vertical*.12f*arc,
        .65f*envelope + .20f*arc,
        vertical*.16f*arc};
}
