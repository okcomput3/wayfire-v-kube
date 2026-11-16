#include "../wf-v-kube/src/searchlight-path.hpp"
#include <cassert>
#include <iostream>
int main()
{
    auto cube = searchlight_boundary(4,0,1,0,1,-1.2f);
    auto normal = searchlight_trace({0,0,0},{0,0,1},3,1.45f,cube);
    assert(normal.size()==2 && normal[0].inside && !normal[1].inside);
    assert(std::abs(normal[0].b.z-.5f)<1e-5f);
    auto ray = glm::normalize(glm::vec3(.4f,0,1));
    auto bent = searchlight_trace({0,0,0},ray,3,1.45f,cube);
    auto exit = glm::normalize(bent[1].b-bent[1].a);
    assert(std::abs(exit.x-1.45f*ray.x)<1e-4f); // Snell's law at +Z face.
    auto straight = searchlight_trace({0,0,0},ray,3,1,cube);
    assert(glm::length(glm::normalize(straight[1].b-straight[1].a)-ray)<1e-5f);
    auto trapped = searchlight_trace({0,0,0},glm::normalize(glm::vec3(.8f,0,1)),4,2,cube);
    assert(trapped.size()>2);
    for (const auto& s : trapped) assert(s.inside);
    auto rows = searchlight_boundary(4,0,1,0,2,-1.2f);
    auto down = searchlight_trace({0,0,0},{0,-1,0},3,1.45f,rows);
    assert(down.size()==4);
    assert(down[0].inside && !down[1].inside && down[2].inside && !down[3].inside);
    auto circle = searchlight_boundary(6,1,1,.3f,1,-1.2f);
    auto circular = searchlight_trace({0,0,0},ray,3,1.45f,circle);
    assert(std::abs(glm::length(circular[0].b)-.5f)<.001f);
    auto star = searchlight_boundary(6,2,1,0,1,-1.2f);
    auto star_ray = searchlight_trace({0,0,0},{0,0,1},3,1.45f,star);
    assert(std::abs(star_ray[0].b.z-1.5f)<.001f);
    auto short_ray = searchlight_trace({0,0,0},ray,.1f,1.45f,cube);
    assert(short_ray.size()==1 && short_ray[0].inside);
    assert(searchlight_boundary(2,0,1,0,1,-1.2f).empty());
    std::cout << "PASS: boundaries, Snell refraction, internal reflection, row entry, cylinder/star, short rays\n";
}
