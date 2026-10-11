#ifndef WF_CUBE_HPP
#define WF_CUBE_HPP

//#include <config.h>
#include <wayfire/option-wrapper.hpp>
#include <wayfire/util/duration.hpp>
#include <wayfire/util/log.hpp>
#include <wayfire/opengl.hpp>

#define TEX_ERROR_FLAG_COLOR  0, 1, 0, 1

using namespace wf::animation;

class cube_animation_t : public duration_t
{
  public:
    using duration_t::duration_t;
    timed_transition_t offset_y{*this};
    timed_transition_t offset_z{*this};
    timed_transition_t rotation{*this};
    timed_transition_t zoom{*this};
    timed_transition_t ease_deformation{*this};
};

struct wf_cube_animation_attribs
{
    wf::option_wrapper_t<wf::animation_description_t> animation_duration{"vertical_kube/initial_animation"};
    cube_animation_t cube_animation{animation_duration};

    // Sampled once by the controller, shared by every output render.
    float frame_rotation = 0, frame_zoom = 1, frame_offset_y = 0;
    float frame_offset_z = 1, frame_ease = 0, frame_time = 0;
    glm::vec2 background_uv_origin{0, 0}, background_uv_scale{1, 1};
    glm::vec2 background_size{1, 1};
    glm::mat4 projection, view;
    float side_angle;

    bool in_exit;
};

#endif /* end of include guard: WF_CUBE_HPP */
