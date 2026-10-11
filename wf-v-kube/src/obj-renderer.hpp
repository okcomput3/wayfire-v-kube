#pragma once
#include <wayfire/opengl.hpp>
#include "obj-loader.hpp"

// All methods must be called with the compositor GL context current.
class cube_obj_renderer_t
{
    std::string loaded_path;
    OpenGL::program_t program;
    GLuint buffer = 0;
    GLsizei count = 0;
    bool uv_top_left = false;
    std::shared_ptr<cube_glb_animation_t> animation;
    std::vector<cube_obj_vertex_t> animated_vertices;
    double animation_start = -1, last_animation_time = -1;
    int last_clip = -2;
    bool last_playing = false;
    std::vector<cube_obj_material_t> materials;
    std::vector<cube_obj_batch_t> batches;
    struct textures_t { GLuint diffuse = 0, specular = 0, opacity = 0; };
    std::vector<textures_t> material_textures;
    std::vector<GLuint> textures;
    void clear_mesh();
  public:
    void load(const std::string& path);
    void restart_animation() { animation_start = -1; last_animation_time = -1; }
    void animate(double frame_time, bool playing, int clip, double speed);
    bool has_animation() const { return bool(animation); }
    void render(const glm::mat4& vp, const glm::mat4& model);
    void release();
};
