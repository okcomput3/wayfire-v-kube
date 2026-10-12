#pragma once
#include <wayfire/opengl.hpp>

// Optional reference-inspired accents; no fullscreen postprocessing or assets.
class cosmic_accents_t
{
    OpenGL::program_t program;
    GLuint vbo = 0;
  public:
    void render(const glm::mat4& vp, const glm::mat4& view, int rows, float spacing,
        int faces, int deform, float rotation, float time, float ease,
        bool rims, bool rings, bool sparkles, float intensity, float speed, float ring_gap, float ring_width);
    void release();
};
