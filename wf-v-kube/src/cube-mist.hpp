#pragma once
#include <wayfire/opengl.hpp>

// Procedural smoke billboards, driven by the shared cube frame clock.
class cube_mist_t
{
    OpenGL::program_t program;
    GLuint vbo = 0;
  public:
    void render(const glm::mat4& vp, const glm::mat4& view, int rows,
        float spacing, int faces, int deformation, float rotation,
        float time, float activation, float density, float speed);
    void release();
};
