#pragma once
#include "cube-background.hpp"

// A flat picture, stretched to the output. Textures load in the GLES subpass.
class wf_cube_image_background : public wf_cube_background_base
{
  public:
    ~wf_cube_image_background() override;
    void render_frame(const wf::render_target_t& target,
        wf_cube_animation_attribs& attribs) override;

  private:
    wf::option_wrapper_t<std::string> image_path{"vertical_kube/background_image"};
    std::string loaded_path;
    GLuint texture = 0;
    void reload_texture();
};
