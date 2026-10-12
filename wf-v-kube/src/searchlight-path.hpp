#pragma once
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <vector>

// Optical envelope follows the same radial deformation as the desktop mesh.
// Decorative cap rounding and bump relief do not change this envelope.
struct searchlight_boundary_t
{
    glm::vec3 a, b, c, normal;
};
struct searchlight_segment_t
{
    glm::vec3 a, b;
    float start, end;
    bool inside;
};
inline std::vector<searchlight_boundary_t> searchlight_boundary(int faces, int deform,
    float ease, float rotation, int rows, float spacing)
{
    std::vector<searchlight_boundary_t> mesh;
    if (faces < 3) return mesh; // Two faces have no enclosed optical volume.
    constexpr float pi = 3.14159265358979323846f;
    float apothem = .5f / std::tan(pi / faces);
    int steps = deform ? 50 : 1;
    std::vector<glm::vec3> rim;
    for (int face = 0; face < faces; ++face)
        for (int step = 0; step < steps; ++step)
        {
            float x = -.5f + float(step) / steps;
            float angle = rotation + face * 2*pi/faces;
            glm::vec3 p(std::cos(angle)*x + std::sin(angle)*apothem, 0,
                -std::sin(angle)*x + std::cos(angle)*apothem);
            float d = glm::length(p);
            if (deform) p *= std::pow(deform == 1 ? .5f/d : d/.5f, ease);
            rim.push_back(p);
        }
    auto triangle = [&] (glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 normal)
    { mesh.push_back({a, b, c, normal}); };
    for (int row = 0; row < rows; ++row)
    {
        glm::vec3 top(0, row*spacing+.5f, 0), bottom(0, row*spacing-.5f, 0);
        for (size_t i = 0; i < rim.size(); ++i)
        {
            auto a = rim[i], b = rim[(i+1)%rim.size()];
            auto normal = glm::normalize(glm::cross(b-a, glm::vec3(0,1,0)));
            triangle(a+bottom,b+bottom,b+top,normal);
            triangle(a+bottom,b+top,a+top,normal);
            triangle(top,a+top,b+top,{0,1,0});
            triangle(bottom,b+bottom,a+bottom,{0,-1,0});
        }
    }
    return mesh;
}
inline bool searchlight_hit(glm::vec3 origin, glm::vec3 direction,
    const searchlight_boundary_t& face, float& distance)
{
    auto edge = face.b-face.a, edge2 = face.c-face.a;
    auto p = glm::cross(direction,edge2);
    float det = glm::dot(edge,p);
    if (std::abs(det)<1e-7f) return false;
    auto t = origin-face.a;
    float u = glm::dot(t,p)/det;
    auto q = glm::cross(t,edge);
    float v = glm::dot(direction,q)/det;
    float d = glm::dot(edge2,q)/det;
    if (u < -1e-5f || v < -1e-5f || u+v > 1.00001f || d < 1e-4f || d >= distance)
        return false;
    distance = d;
    return true;
}
inline std::vector<searchlight_segment_t> searchlight_trace(glm::vec3 origin,
    glm::vec3 direction, float reach, float index,
    const std::vector<searchlight_boundary_t>& mesh)
{
    std::vector<searchlight_segment_t> result;
    float travelled = 0;
    bool inside = !mesh.empty(); // Sources are placed at their row's centre.
    for (int bounce = 0; bounce < 12 && travelled < reach; ++bounce)
    {
        float distance = reach-travelled;
        const searchlight_boundary_t *hit = nullptr;
        for (const auto& face : mesh)
            if (searchlight_hit(origin,direction,face,distance)) hit = &face;
        auto end = origin + direction*distance;
        result.push_back({origin,end,travelled,travelled+distance,inside});
        travelled += distance;
        if (!hit) break;
        bool leaving = glm::dot(direction,hit->normal)>0;
        auto normal = leaving ? -hit->normal : hit->normal;
        auto refracted = glm::refract(direction,normal,leaving ? index : 1.f/index);
        if (glm::dot(refracted,refracted)<1e-6f)
            direction = glm::reflect(direction,normal); // Total internal reflection.
        else
        {
            direction = glm::normalize(refracted);
            inside = !leaving;
        }
        origin = end; // The intersection epsilon rejects the surface just crossed.
    }
    return result;
}
