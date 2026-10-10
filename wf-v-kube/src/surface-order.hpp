#pragma once
#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>
#include <glm/glm.hpp>
#include <functional>

struct cube_surface_bounds_t
{
    float min_y, max_y, depth;
    bool cap;
    std::vector<glm::vec3> outline;
    glm::vec4 plane{0.0f};
};

struct cube_surface_piece_t
{
    int surface;
    std::vector<glm::vec3> outline;
};

// Split polygons at separating planes before sorting. Whole-face sorting can
// have cycles when popped windows span caps or adjacent workspace rows.
inline std::vector<cube_surface_piece_t> cube_planar_order(
    const std::vector<cube_surface_bounds_t>& surfaces, glm::vec3 eye)
{
    std::vector<cube_surface_piece_t> input, result;
    for (int i = 0; i < int(surfaces.size()); ++i)
        input.push_back({i, surfaces[i].outline});
    std::function<void(std::vector<cube_surface_piece_t>)> visit;
    visit = [&] (std::vector<cube_surface_piece_t> pieces)
    {
        if (pieces.empty()) return;
        const auto plane = surfaces[pieces.front().surface].plane;
        const auto normal = glm::vec3(plane);
        std::vector<cube_surface_piece_t> front, back, coplanar;
        for (auto& piece : pieces)
        {
            bool positive = false, negative = false;
            for (auto p : piece.outline)
            {
                float d = glm::dot(normal, p) + plane.w;
                positive |= d > 1e-5f;
                negative |= d < -1e-5f;
            }
            if (!positive && !negative) coplanar.push_back(std::move(piece));
            else if (!negative) front.push_back(std::move(piece));
            else if (!positive) back.push_back(std::move(piece));
            else
            {
                cube_surface_piece_t f{piece.surface, {}}, b{piece.surface, {}};
                for (size_t i = 0; i < piece.outline.size(); ++i)
                {
                    auto a = piece.outline[i], z = piece.outline[(i + 1) % piece.outline.size()];
                    float da = glm::dot(normal, a) + plane.w;
                    float dz = glm::dot(normal, z) + plane.w;
                    if (da >= 0) f.outline.push_back(a);
                    if (da <= 0) b.outline.push_back(a);
                    if ((da < 0 && dz > 0) || (da > 0 && dz < 0))
                    {
                        auto intersection = a + (z - a) * (da / (da - dz));
                        f.outline.push_back(intersection);
                        b.outline.push_back(intersection);
                    }
                }
                if (f.outline.size() >= 3) front.push_back(std::move(f));
                if (b.outline.size() >= 3) back.push_back(std::move(b));
            }
        }
        const bool eye_front = glm::dot(normal, eye) + plane.w >= 0;
        visit(eye_front ? std::move(back) : std::move(front));
        for (auto& p : coplanar) result.push_back(std::move(p));
        visit(eye_front ? std::move(front) : std::move(back));
    };
    visit(std::move(input));
    return result;
}

// Horizontal caps separate surfaces by plane, not by projected center depth.
// Preserve far-to-near center ordering only where no separating cap applies.
inline std::vector<int> cube_surface_order(
    const std::vector<cube_surface_bounds_t>& surfaces, float eye_y)
{
    const int count = surfaces.size();
    std::vector<std::vector<int>> edges(count);
    std::vector<int> incoming(count, 0), preference(count), result;
    std::iota(preference.begin(), preference.end(), 0);
    std::stable_sort(preference.begin(), preference.end(), [&] (int a, int b)
    {
        return surfaces[a].depth > surfaces[b].depth;
    });
    for (int cap = 0; cap < count; ++cap)
    {
        if (!surfaces[cap].cap) continue;
        const float plane = surfaces[cap].min_y;
        if (std::abs(eye_y - plane) < 1e-5f) continue;
        for (int other = 0; other < count; ++other)
        {
            if (other == cap) continue;
            const auto& bounds = surfaces[other];
            int side = 0;
            if (bounds.min_y >= plane - 1e-5f && bounds.max_y > plane + 1e-5f) side = 1;
            if (bounds.max_y <= plane + 1e-5f && bounds.min_y < plane - 1e-5f) side = -1;
            if (!side) continue; // Coplanar or crossing: no whole-surface ordering.
            const bool nearer = (eye_y > plane) == (side > 0);
            // Surfaces across the eye cannot overlap along a forward ray.
            // Do not create contradictory constraints between cubes above
            // and below the viewer. A face spanning eye height still overlaps
            // the cap and must retain its constraint for transparent rendering.
            if (nearer && ((eye_y > plane && bounds.min_y >= eye_y) ||
                (eye_y < plane && bounds.max_y <= eye_y))) continue;
            int before = nearer ? cap : other;
            int after = nearer ? other : cap;
            edges[before].push_back(after);
            ++incoming[after];
        }
    }
    std::vector<bool> emitted(count, false);
    while (int(result.size()) < count)
    {
        int next = -1;
        for (int candidate : preference)
            if (!emitted[candidate] && incoming[candidate] == 0) { next = candidate; break; }
        // Defensive fallback for malformed/nonfinite geometry.
        if (next < 0)
            for (int candidate : preference)
                if (!emitted[candidate]) { next = candidate; break; }
        emitted[next] = true;
        result.push_back(next);
        for (int after : edges[next]) --incoming[after];
    }
    return result;
}
