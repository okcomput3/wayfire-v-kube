#pragma once
#include "cube-background.hpp"

class wf_cube_shader_background : public wf_cube_background_base
{
  public:
    wf_cube_shader_background();
    ~wf_cube_shader_background() override;
    void render_frame(const wf::render_target_t& target,
        wf_cube_animation_attribs& attribs) override;

  private:
    wf::option_wrapper_t<double> resolution_scale{"vertical_kube/shader_resolution_scale"};
    OpenGL::program_t background_program, copy_program;
    wf::auxilliary_buffer_t scaled_buffer;
    GLuint background_vbo = 0;
};
