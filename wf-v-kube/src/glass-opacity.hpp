#pragma once
#include <algorithm>

// Follow the rendered camera position, not the pending animation destination.
// The desktop is opaque at its normal distance and reaches the user's glass
// opacity at the open-cube distance. Smoothstep avoids a visible knee.
inline float cube_glass_opacity(float configured, float opening, float zoom,
    float camera_distance, float normal_distance, float opening_distance)
{
    float distance = (camera_distance * zoom - normal_distance) /
        std::max(opening_distance, 0.1f);
    float progress = std::clamp(distance, 0.0f, 1.0f);
    progress = progress * progress * (3.0f - 2.0f * progress);
    progress *= std::clamp(opening, 0.0f, 1.0f);
    return 1.0f + (std::clamp(configured, 0.0f, 1.0f) - 1.0f) * progress;
}
