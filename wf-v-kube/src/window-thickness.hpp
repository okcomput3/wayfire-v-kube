#pragma once
#include <wayfire/opengl.hpp>
class window_thickness_t
{
    OpenGL::program_t program;
  public:
    void render(wf::auxilliary_buffer_t& pixels, const glm::mat4& vp,
        const glm::mat4& model, float thickness, float cutoff, float opacity,
        int deform, float ease, float scale, bool desktop = false);
    void release() { program.free_resources(); }
};
