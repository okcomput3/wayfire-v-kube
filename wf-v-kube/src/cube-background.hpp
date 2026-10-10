#ifndef WF_CUBE_BACKGROUND_HPP
#define WF_CUBE_BACKGROUND_HPP

#include <wayfire/opengl.hpp>
#include "cube.hpp"

class wf_cube_background_base
{
  public:
    virtual void render_frame(const wf::render_target_t& fb,
        wf_cube_animation_attribs& attribs) = 0;
    virtual ~wf_cube_background_base() = default;

  protected:
    wf::option_wrapper_t<wf::color_t> fallback_color{"vertical_kube/background"};
};

#endif /* end of include guard: WF_CUBE_BACKGROUND_HPP */
