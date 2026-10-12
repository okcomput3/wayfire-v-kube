#include "popout-compositor.hpp"
#include <wayfire/core.hpp>
#include <wayfire/config/config-manager.hpp>
#include <wayfire/workspace-set.hpp>
#include <wayfire/view.hpp>

using damage_region_t = decltype(wf::scene::render_instruction_t::damage);

static const char *resolve_vertex = R"(#version 100
attribute highp vec2 position;
varying highp vec2 uv;
void main() {
    gl_Position = vec4(position, 0.0, 1.0);
    uv = position * 0.5 + 0.5;
})";

// This shader only extracts coverage. The blur itself is rendered by the
// window's existing blur transformer, with its provider and selected method.
static const char *coverage_fragment = R"(#version 100
precision highp float;
varying highp vec2 uv;
uniform sampler2D window_tex;
uniform float blurred;
uniform float alpha_threshold;
uniform float alpha_exponent;
void main() {
    float a = texture2D(window_tex, uv).a;
    float blur_coverage = blurred * pow(clamp(a / alpha_threshold, 0.0, 1.0), alpha_exponent);
    float coverage = a + (1.0 - a) * blur_coverage;
    gl_FragColor = vec4(coverage);
})";

static const char *resolve_fragment = R"(#version 100
precision highp float;
varying highp vec2 uv;
uniform sampler2D composite_tex;
uniform sampler2D coverage_tex;
uniform sampler2D backdrop_tex;
void main() {
    float alpha = texture2D(coverage_tex, uv).a;
    vec3 backdrop = texture2D(backdrop_tex, uv).rgb;
    vec3 color = texture2D(composite_tex, uv).rgb - (1.0 - alpha) * backdrop;
    if (alpha < 0.5 / 255.0) {
        gl_FragColor = vec4(0.0);
    } else {
        gl_FragColor = vec4(clamp(color, 0.0, alpha), alpha);
    }
})";

namespace
{
// Last in front-to-back scheduling, hence drawn before the windows and blur.
class backdrop_instance_t : public wf::scene::render_instance_t
{
    wf::auxilliary_buffer_t& buffer;
  public:
    explicit backdrop_instance_t(wf::auxilliary_buffer_t& buffer) : buffer(buffer) {}
    void schedule_instructions(std::vector<wf::scene::render_instruction_t>& instructions,
        const wf::render_target_t& target, damage_region_t& damage) override
    {
        instructions.push_back({.instance = this, .target = target, .damage = damage});
    }
    void render(const wf::scene::render_instruction_t& data) override
    {
        data.pass->custom_gles_subpass([&]
        {
            // RGBX gives us an opaque reference even with translucent wallpaper.
            auto texture = wf::gles_texture_t::from_aux(buffer);
            texture.type = wf::TEXTURE_TYPE_RGBX;
            OpenGL::render_texture(texture, data.target, data.target.geometry);
        });
    }
};
}

popout_compositor_t::~popout_compositor_t()
{
    wf::gles::run_in_context_if_gles([&]
    {
        program.free_resources();
        coverage_program.free_resources();
    });
}

namespace
{
bool has_blur(const wf::scene::node_ptr& node)
{
    if (node->stringify() == "blur")
    {
        return true;
    }
    for (const auto& child : node->get_children())
    {
        if (child->is_enabled() && has_blur(child))
        {
            return true;
        }
    }
    return false;
}

double blur_option(const char *name, double fallback)
{
    auto option = wf::get_core().config->get_option<double>(name);
    return option ? option->get_value() : fallback;
}

const GLfloat quad[] = {-1, -1, 1, -1, -1, 1, 1, 1};

void draw_region(const wf::render_target_t& target, const damage_region_t& damage)
{
#ifdef VERTICAL_KUBE_SCISSOR_REGIONS
    wf::gles::for_each_scissor_rect(target, damage, [&]
    {
        GL_CALL(glDrawArrays(GL_TRIANGLE_STRIP, 0, 4));
    });
#else
    for (const auto& rect : damage)
    {
        wf::gles::render_target_logic_scissor(target,
            {rect.x1, rect.y1, rect.x2 - rect.x1, rect.y2 - rect.y1});
        GL_CALL(glDrawArrays(GL_TRIANGLE_STRIP, 0, 4));
    }
#endif
    GL_CALL(glDisable(GL_SCISSOR_TEST));
}
}

void popout_compositor_t::begin_frame(wf::output_t *output)
{
    // Snapshot once per frame, not once per cube face. Refreshing each frame
    // preserves animated effects, restacking, and live blur configuration.
    windows.clear();
    const auto views = output->wset()->get_views(wf::WSET_SORT_STACKING |
        wf::WSET_MAPPED_ONLY | wf::WSET_EXCLUDE_MINIMIZED);
    for (const auto& view : views)
    {
        auto node = view->get_root_node();
        windows.push_back({node, has_blur(node)});
    }
    alpha_threshold = blur_option("blur/alpha_threshold", 0.5);
    alpha_exponent = blur_option("blur/alpha_exponent", 1.0);
}

void popout_compositor_t::render(std::vector<wf::scene::render_instance_uptr>& instances,
    const wf::render_target_t& target, wf::auxilliary_buffer_t& backdrop,
    wf::output_t *output)
{
    auto run = [&] (wf::auxilliary_buffer_t& buffer,
        std::vector<wf::scene::render_instance_uptr>& list, bool feedback, const damage_region_t& damage)
    {
        buffer.allocate(wf::dimensions(target.geometry), target.scale);
        wf::render_target_t pass_target{buffer};
        pass_target.geometry = target.geometry;
        pass_target.scale = target.scale;
        wf::render_pass_params_t params;
        params.instances = &list;
        params.target = pass_target;
        params.damage = damage;
        params.background_color = {0, 0, 0, 0};
        params.reference_output = feedback ? output : nullptr;
        params.flags = wf::RPASS_CLEAR_BACKGROUND | wf::RPASS_EMIT_SIGNALS;
        wf::render_pass_t::run(params);
    };

    bool needs_backdrop = false;
    for (const auto& entry : windows)
    {
        damage_region_t overlap{entry.node->get_bounding_box()};
        overlap.expand_edges(1);
        overlap &= target.geometry;
        if (!overlap.empty() && entry.blurred)
        {
            needs_backdrop = true;
            break;
        }
    }
    if (!needs_backdrop)
    {
        wf::render_pass_params_t params;
        params.instances = &instances;
        params.target = target;
        params.damage = target.geometry;
        params.background_color = {0, 0, 0, 0};
        params.reference_output = output;
        params.flags = wf::RPASS_CLEAR_BACKGROUND | wf::RPASS_EMIT_SIGNALS;
        wf::render_pass_t::run(params);
        return;
    }

    coverage.allocate(wf::dimensions(target.geometry), target.scale);
    wf::gles::run_in_context([&]
    {
        if (!coverage_program.get_program_id(wf::TEXTURE_TYPE_RGBA))
        {
            coverage_program.set_simple(OpenGL::compile_program(resolve_vertex, coverage_fragment));
            program.set_simple(OpenGL::compile_program(resolve_vertex, resolve_fragment));
        }
        wf::gles::bind_render_buffer(coverage.get_renderbuffer());
        GL_CALL(glDisable(GL_SCISSOR_TEST));
        OpenGL::clear({0, 0, 0, 0});
    });

    // Measure each window's real alpha after its normal transforms. Rendering
    // over transparent black leaves no backdrop alpha in the native blur result.
    // Accumulate native blur coverage per window, so mixed blurred/unblurred
    // windows and overlapping translucent windows do not use one global mask.
    damage_region_t occupied;
    for (const auto& entry : windows)
    {
        damage_region_t window_damage{entry.node->get_bounding_box()};
        // Include fractional-scale edge pixels, then clamp to this workspace.
        window_damage.expand_edges(1);
        window_damage &= target.geometry;
        if (window_damage.empty())
        {
            continue;
        }
        occupied |= window_damage;
        std::vector<wf::scene::render_instance_uptr> single_window;
        entry.node->gen_render_instances(single_window,
            [] (const damage_region_t&) {}, output);
        run(window, single_window, false, window_damage);
        wf::gles::run_in_context([&]
        {
            wf::gles::bind_render_buffer(coverage.get_renderbuffer());
            GL_CALL(glDisable(GL_SCISSOR_TEST));
            GL_CALL(glDisable(GL_DEPTH_TEST));
            GL_CALL(glEnable(GL_BLEND));
            GL_CALL(glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA));
            coverage_program.use(wf::TEXTURE_TYPE_RGBA);
            coverage_program.uniform1i("window_tex", 0);
            coverage_program.uniform1f("blurred", entry.blurred ? 1.0f : 0.0f);
            coverage_program.uniform1f("alpha_threshold", alpha_threshold);
            coverage_program.uniform1f("alpha_exponent", alpha_exponent);
            GL_CALL(glActiveTexture(GL_TEXTURE0));
            GL_CALL(glBindTexture(GL_TEXTURE_2D, wf::gles_texture_t::from_aux(window).tex_id));
            coverage_program.attrib_pointer("position", 2, 0, quad);
            wf::render_target_t coverage_target{coverage};
            coverage_target.geometry = target.geometry;
            coverage_target.scale = target.scale;
            // Pixels outside the window cannot contribute to its coverage.
            // They may contain scratch data from the previous window.
            draw_region(coverage_target, window_damage);
            coverage_program.deactivate();
            GL_CALL(glBindTexture(GL_TEXTURE_2D, 0));
        });
    }

    // The real color pass uses the same existing render instances and native
    // blur transformer as the desktop. Supply wallpaper behind the windows.
    instances.push_back(std::make_unique<backdrop_instance_t>(backdrop));
    // Native blur scheduling expands this damage for its sampling footprint.
    // Keep the original union for extraction: scratch pixels outside it need
    // neither compositing nor sampling, and may belong to a previous face.
    run(composite, instances, true, occupied);
    instances.pop_back();

    wf::gles::run_in_context([&]
    {
        wf::gles::bind_render_buffer(target);
        GL_CALL(glDisable(GL_BLEND));
        GL_CALL(glDisable(GL_DEPTH_TEST));
        GL_CALL(glDisable(GL_SCISSOR_TEST));
        // Erase old window positions before drawing only the current regions.
        OpenGL::clear({0, 0, 0, 0});
        program.use(wf::TEXTURE_TYPE_RGBA);
        wf::auxilliary_buffer_t *buffers[] = {&composite, &coverage, &backdrop};
        const char *names[] = {"composite_tex", "coverage_tex", "backdrop_tex"};
        for (int i = 0; i < 3; ++i)
        {
            GL_CALL(glActiveTexture(GL_TEXTURE0 + i));
            GL_CALL(glBindTexture(GL_TEXTURE_2D, wf::gles_texture_t::from_aux(*buffers[i]).tex_id));
            program.uniform1i(names[i], i);
        }
        program.attrib_pointer("position", 2, 0, quad);
        draw_region(target, occupied);
        program.deactivate();
        for (int i = 2; i >= 0; --i)
        {
            GL_CALL(glActiveTexture(GL_TEXTURE0 + i));
            GL_CALL(glBindTexture(GL_TEXTURE_2D, 0));
        }
        GL_CALL(glEnable(GL_BLEND));
        GL_CALL(glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA));
    });
}

void popout_compositor_t::render_separate(const wf::render_target_t& target,
    wf::auxilliary_buffer_t& backdrop, wf::output_t *output, int face, int row,
    std::vector<popout_window_layer_t>& layers, size_t& used, bool capture_edges)
{
    auto ordered = windows;
    stacked_backdrop.allocate(wf::dimensions(target.geometry), target.scale);
    wf::render_target_t stack_target{stacked_backdrop};
    stack_target.geometry = target.geometry;
    stack_target.scale = target.scale;
    wf::gles::run_in_context([&]
    {
        wf::gles::bind_render_buffer(stack_target);
        GL_CALL(glDisable(GL_SCISSOR_TEST));
        OpenGL::clear({0, 0, 0, 0});
        OpenGL::render_texture(wf::gles_texture_t::from_aux(backdrop), stack_target, stack_target.geometry);
    });
    // Wayfire returns front-to-back stacking. Build native blur backdrops from
    // the bottom upwards, but preserve the same rank on every workspace slice.
    for (int index = int(ordered.size()) - 1; index >= 0; --index)
    {
        const auto entry = ordered[index];
        damage_region_t overlap{entry.node->get_bounding_box()};
        overlap.expand_edges(1);
        overlap &= target.geometry;
        if (overlap.empty()) continue;
        if (used == layers.size()) layers.emplace_back();
        auto& layer = layers[used++];
        layer.face = face; layer.row = row; layer.rank = int(ordered.size()) - 1 - index;
        layer.buffer.allocate(wf::dimensions(target.geometry), target.scale);
        wf::render_target_t single_target{layer.buffer};
        single_target.geometry = target.geometry;
        single_target.scale = target.scale;
        std::vector<wf::scene::render_instance_uptr> instances;
        entry.node->gen_render_instances(instances, [] (const damage_region_t&) {}, output);
        if (capture_edges)
        {
            // Capture against transparent black BEFORE adding any blur backdrop.
            // Native blur retains the window's actual alpha in this pass.
            layer.edge_pixels.allocate(wf::dimensions(target.geometry),target.scale);
            wf::render_target_t edge_target{layer.edge_pixels};
            edge_target.geometry=target.geometry; edge_target.scale=target.scale;
            wf::render_pass_params_t params;
            params.instances=&instances; params.target=edge_target;
            params.damage=target.geometry; params.background_color={0,0,0,0};
            params.reference_output=nullptr;
            params.flags=wf::RPASS_CLEAR_BACKGROUND | wf::RPASS_EMIT_SIGNALS;
            wf::render_pass_t::run(params);
        } else layer.edge_pixels.free();
        windows = {entry};
        render(instances, single_target, stacked_backdrop, output);
        wf::gles::run_in_context([&]
        {
            wf::gles::bind_render_buffer(stack_target);
            GL_CALL(glDisable(GL_DEPTH_TEST));
            GL_CALL(glDisable(GL_SCISSOR_TEST));
            OpenGL::render_texture(wf::gles_texture_t::from_aux(layer.buffer), stack_target, stack_target.geometry);
        });
    }
    windows = std::move(ordered);
}
