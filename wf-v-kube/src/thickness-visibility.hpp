#pragma once
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>

// Conservative bound for a texture rectangle, including deformation and its
// inward extrusion. Shared by desktop and window passes, with no facing test:
// rear geometry may still be visible through glass or around a cap.
inline bool thickness_visible(const glm::mat4& vp, const glm::mat4& model,
    glm::vec2 low, glm::vec2 high, int deform, float ease, float scale, float thickness)
{
    auto middle=(low+high)*.5f;
    auto extent=(high-low)*.5f;
    auto center=glm::vec3(model*glm::vec4(middle,0,1));
    float radius=glm::length(glm::vec3(model[0]))*extent.x+
        glm::length(glm::vec3(model[1]))*extent.y;
    if (deform && ease>0)
    {
        float r=glm::length(glm::vec2(center.x,center.z));
        float reference=.5f*scale;
        float radial_extent=glm::length(glm::vec2(model[0].x,model[0].z))*extent.x+
            glm::length(glm::vec2(model[1].x,model[1].z))*extent.y;
        float minimum=std::max(r-radial_extent,1e-6f);
        float maximum=r+radial_extent;
        // Maximum derivative of p * (reference/|p|)^ease (cylinder),
        // or p * (|p|/reference)^ease (star), over this bounding sphere.
        float lipschitz=deform==1 ? std::pow(reference/minimum,ease) :
            (1+ease)*std::pow(maximum/reference,ease);
        float factor=std::pow(deform==1 ? reference/std::max(r,1e-6f) :
            std::max(r,1e-6f)/reference,ease);
        center.x*=factor; center.z*=factor;
        float vertical_extent=std::abs(model[0].y)*extent.x+std::abs(model[1].y)*extent.y;
        radius=std::hypot(lipschitz*radial_extent,vertical_extent);
    }
    radius+=thickness*scale + .0001f;
    for (int axis=0; axis<3; ++axis)
        for (int sign : {-1,1})
        {
            glm::vec4 plane;
            for (int column=0; column<4; ++column)
                plane[column]=vp[column][3]+float(sign)*vp[column][axis];
            if (glm::dot(plane,glm::vec4(center,1)) < -radius*glm::length(glm::vec3(plane)))
                return false;
        }
    return true;
}
