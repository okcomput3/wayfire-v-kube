#pragma once
#include <wayfire/opengl.hpp>
#include <wayfire/scene-render.hpp>

struct popout_window_layer_t
{
    wf::auxilliary_buffer_t buffer;
    int face = 0, row = 0, rank = 0;
};

// Resolve windows rendered over a backdrop back into a transparent popout layer.
class popout_compositor_t
{
  public:
    ~popout_compositor_t();
    void render_separate(const wf::render_target_t& target, wf::auxilliary_buffer_t& backdrop,
        wf::output_t *output, int face, int row, std::vector<popout_window_layer_t>& layers, size_t& used);
    void begin_frame(wf::output_t *output);
    void render(std::vector<wf::scene::render_instance_uptr>& instances,
        const wf::render_target_t& target, wf::auxilliary_buffer_t& backdrop,
        wf::output_t *output);

  private:
    struct window_entry_t
    {
        wf::scene::node_ptr node;
        bool blurred;
    };
    std::vector<window_entry_t> windows;
    double alpha_threshold = 0.5, alpha_exponent = 1.0;
    wf::auxilliary_buffer_t composite, coverage, window, stacked_backdrop;
    OpenGL::program_t program, coverage_program;
};
