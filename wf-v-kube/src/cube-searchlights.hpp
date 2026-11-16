#pragma once
#include <wayfire/opengl.hpp>

// Stylized additive searchlight beams. Shares the cube's sampled clock and VP.
class cube_searchlights_t
{
    OpenGL::program_t program;
    GLuint vbo = 0;
  public:
    void render(const glm::mat4& vp, const glm::vec3& view_direction, int rows, float row_spacing, float rotation,
        float time, float activation, float speed, float intensity, float length, int count,
        bool refract, float refractive_index, int faces, int deformation);
    void release();
};
