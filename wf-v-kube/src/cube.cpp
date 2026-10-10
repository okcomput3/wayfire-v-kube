#include <wayfire/per-output-plugin.hpp>
#include <memory>
#include <wayfire/plugin.hpp>
#include <wayfire/opengl.hpp>
#include <wayfire/output.hpp>
#include <wayfire/core.hpp>
#include <wayfire/config/config-manager.hpp>
#include <wayfire/workspace-stream.hpp>
#include <wayfire/render-manager.hpp>
#include <wayfire/workspace-set.hpp>
#include <wayfire/scene-operations.hpp>
#include <wayfire/plugins/common/input-grab.hpp>
#include "wayfire/plugins/ipc/ipc-activator.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <wayfire/img.hpp>

#include "cube.hpp"
#include "simple-background.hpp"
#include "skydome.hpp"
#include "cubemap.hpp"
#include "shader-background.hpp"
#include "image-background.hpp"
#include "popout-compositor.hpp"
#include "scroll-momentum.hpp"
#include "glass-opacity.hpp"
#include "surface-order.hpp"
#include <wayfire/plugins/blur/blur.hpp>
#include "cube-control-signal.hpp"
#include "wayfire/region.hpp"
#include "wayfire/scene-render.hpp"
#include "wayfire/scene.hpp"
#include "wayfire/signal-definitions.hpp"
#include <chrono>

// Master uses floating-point scene damage; retain compatibility with 0.11.
using cube_damage_region_t = decltype(wf::scene::render_instruction_t::damage);

#define Z_OFFSET_NEAR 0.89567f
#define Z_OFFSET_FAR  2.00000f

#define ZOOM_MAX 10.0f
#define ZOOM_MIN 0.1f


#define CUBE_VERTICAL_SPACING -1.2f

#ifdef VERTICAL_KUBE_USE_GLES32
    #include <GLES3/gl32.h>
    #include "shaders-3-2.tpp"
#endif

#include "shaders.tpp"

// Vertex shader - updated to pass world position
static const char *cube_cap_vertex = R"(
#version 100
attribute highp vec2 position;
attribute mediump vec2 uvPosition;

uniform mat4 VP;
uniform mat4 model;
uniform int deform;
uniform float ease;

varying mediump vec2 uvpos;
varying mediump vec3 worldPos;

void main() {
    uvpos = uvPosition;
    vec4 worldPosition = model * vec4(position.x, 0.0, position.y, 1.0);
    if (deform > 0) {
        float d = max(length(worldPosition.xz), 0.000001);
        float factor = deform == 1 ? 0.5 / d : d / 0.5;
        worldPosition.xz *= pow(factor, ease);
    }
    worldPos = worldPosition.xyz;
    gl_Position = VP * worldPosition;
}
)";
/*
// Fragment shader - animated with wave effects
static const char *cube_cap_fragment = R"(
#version 100
precision mediump float;

varying mediump vec2 uvpos;
varying mediump vec3 worldPos;
uniform sampler2D smp;
uniform float cap_alpha;
uniform bool depth_only;
uniform bool translucent_only;
uniform float time;  // Add this uniform

void main() {
    // Calculate distance from center using UV coordinates
    vec2 centerUV = uvpos - vec2(0.5, 0.5);
    float dist = length(centerUV) * 2.0;  // Normalize to 0-1 range
    
    // Wave parameters
    float frequency = 40.0;
    float speed = 1.8;
    float amplitude = 0.1;
    
    // Calculate wave height
    float height = sin(dist * frequency - time * speed) * amplitude;
    
    // Calculate gradients for normal mapping
    float delta = 0.01;
    
    // X gradient
    vec2 uvX1 = uvpos + vec2(delta, 0.0);
    vec2 uvX2 = uvpos - vec2(delta, 0.0);
    float distX1 = length((uvX1 - vec2(0.5, 0.5)) * 2.0);
    float distX2 = length((uvX2 - vec2(0.5, 0.5)) * 2.0);
    float hX1 = sin(distX1 * frequency - time * speed) * amplitude;
    float hX2 = sin(distX2 * frequency - time * speed) * amplitude;
    float dx = (hX1 - hX2) / (2.0 * delta);
    
    // Y gradient
    vec2 uvY1 = uvpos + vec2(0.0, delta);
    vec2 uvY2 = uvpos - vec2(0.0, delta);
    float distY1 = length((uvY1 - vec2(0.5, 0.5)) * 2.0);
    float distY2 = length((uvY2 - vec2(0.5, 0.5)) * 2.0);
    float hY1 = sin(distY1 * frequency - time * speed) * amplitude;
    float hY2 = sin(distY2 * frequency - time * speed) * amplitude;
    float dy = (hY1 - hY2) / (2.0 * delta);
    
    // Calculate normal from gradients
    vec3 normal = normalize(vec3(-dx, -dy, 1.0));
    
    // Animated light direction
    vec3 lightDir = normalize(vec3(0.3, sin(time * 0.2), 0.5));
    
    // Calculate lighting
    float brightness = clamp(exp(dot(normal, lightDir)) * 0.5, 0.0, 1.0);
    
    // Get base color from texture
    vec4 texColor = texture2D(smp, uvpos);
    
    // Apply lighting and alpha
    vec3 finalColor = texColor.rgb * brightness;
    gl_FragColor = vec4(finalColor, texColor.a * 1.0);
}
)";*/
static const char *cube_cap_fragment = R"(
#version 100
precision mediump float;
varying mediump vec2 uvpos;
varying mediump vec3 worldPos;
uniform sampler2D smp;
uniform float cap_alpha;
uniform bool depth_only;
uniform bool translucent_only;
uniform float time;
uniform float u_num_sides;


uniform bool dynamic_lighting;
uniform highp float lighting_time;
uniform highp float lighting_row;
uniform highp float lighting_radius;
uniform mediump float lighting_strength;

// Three independently phased point lights orbit within this row's volume.
// Apply only to RGB: coverage, transparency, blur masks and depth stay intact.
highp vec3 interiorLights(highp vec3 world) {
    if (!dynamic_lighting) return vec3(0.0);
    highp vec3 p = world - vec3(0.0, lighting_row, 0.0);
    highp float radius = max(lighting_radius, 0.1);
    highp vec3 result = vec3(0.0);
    for (int i = 0; i < 3; ++i) {
        highp float phase = float(i) * 2.0943951 + lighting_row * 0.73;
        highp float angle = lighting_time * (0.8 + float(i) * 0.13) + phase;
        highp vec3 point = vec3(radius * 0.58 * cos(angle),
            0.30 * sin(lighting_time * 0.71 + phase), radius * 0.58 * sin(angle));
        highp vec3 delta = (p - point) / vec3(radius, 0.5, radius);
        highp float attenuation = 1.0 / (1.0 + 5.0 * dot(delta, delta));
        highp vec3 hue = 0.5 + 0.5 * cos(lighting_time * 0.35 + phase + vec3(0.0, 2.0943951, 4.1887902));
        result += hue * attenuation;
    }
    return result * lighting_strength;
}

uniform float cap_apothem;
uniform float cap_uv_radius;

// Rounded regular-polygon distance in the undeformed cap plane. The cap UVs
// follow the tessellated mesh, so this outline follows cylinder/star morphs
// without imposing an unrelated square or circular mask on their perimeter.
float capEdgeCoverage() {
    if (u_num_sides < 3.0) return 1.0;
    vec2 p = (uvpos - 0.5) * (2.0 * cap_uv_radius);
    float half_sector = 3.14159265 / u_num_sides;
    float angle = atan(p.x, p.y);
    angle = mod(angle + half_sector, 2.0 * half_sector) - half_sector;
    vec2 folded = length(p) * vec2(abs(sin(angle)), cos(angle));
    float radius = min(0.08, cap_apothem * 0.2);
    float inset = cap_apothem - radius;
    vec2 edge = vec2(max(folded.x - inset * tan(half_sector), 0.0), folded.y - inset);
    float distance = length(edge) * sign(edge.y) - radius;
    return 1.0 - smoothstep(-0.012, 0.0, distance);
}

void main() {
    // Calculate distance from center using UV coordinates
    vec2 centerUV = uvpos - vec2(0.5, 0.5);
    float dist = length(centerUV) * 2.0;
    
    // Wave parameters
    float frequency = 40.0;
    float speed = 1.8;
    float amplitude = 0.1;
    
    // Calculate wave height
    float height = sin(dist * frequency - time * speed) * amplitude;
    
    // Calculate gradients for normal mapping
    float delta = 0.01;
    
    // X gradient
    vec2 uvX1 = uvpos + vec2(delta, 0.0);
    vec2 uvX2 = uvpos - vec2(delta, 0.0);
    float distX1 = length((uvX1 - vec2(0.5, 0.5)) * 2.0);
    float distX2 = length((uvX2 - vec2(0.5, 0.5)) * 2.0);
    float hX1 = sin(distX1 * frequency - time * speed) * amplitude;
    float hX2 = sin(distX2 * frequency - time * speed) * amplitude;
    float dx = (hX1 - hX2) / (2.0 * delta);
    
    // Y gradient
    vec2 uvY1 = uvpos + vec2(0.0, delta);
    vec2 uvY2 = uvpos - vec2(0.0, delta);
    float distY1 = length((uvY1 - vec2(0.5, 0.5)) * 2.0);
    float distY2 = length((uvY2 - vec2(0.5, 0.5)) * 2.0);
    float hY1 = sin(distY1 * frequency - time * speed) * amplitude;
    float hY2 = sin(distY2 * frequency - time * speed) * amplitude;
    float dy = (hY1 - hY2) / (2.0 * delta);
    
    // Calculate normal from gradients
    vec3 normal = normalize(vec3(-dx, -dy, 1.0));
    
    // Animated light direction
    vec3 lightDir = normalize(vec3(0.3, sin(time * 0.2), 0.5));
    
    // Calculate lighting
    float brightness = clamp(exp(dot(normal, lightDir)) * 0.5, 0.0, 1.0);
    
    // Get base color from texture
    vec4 texColor = texture2D(smp, uvpos);
    
    // Apply lighting
    vec3 finalColor = texColor.rgb * brightness;
    
    // Scale color and alpha together so soft glass edges stay premultiplied.
    float alpha = texColor.a * cap_alpha * capEdgeCoverage();
    if (alpha <= 0.0 || (depth_only && alpha < 0.999)) discard;
    if (translucent_only && alpha >= 0.999) discard;
    finalColor = min(vec3(1.0), finalColor + interiorLights(worldPos));
    gl_FragColor = vec4(finalColor * alpha, alpha);
}
)";

class wayfire_cube : public wf::per_output_plugin_instance_t, public wf::pointer_interaction_t
{
    wf::animation::simple_animation_t popout_scale_animation{wf::create_option<int>(300),
        [] (double t) { return t * t * (3.0 - 2.0 * t); }};
    class cube_render_node_t : public wf::scene::node_t
    {

// Custom node that filters to show only windows (no background/desktop)
class windows_only_workspace_node_t : public wf::scene::node_t
{
    wf::output_t *output;
    wf::point_t workspace;
    
  public:
    // This proxy is outside the scenegraph. Forward stacking changes locally
    // so its render-instance manager does not keep a stale window order.
    wf::signal::connection_t<wf::scene::root_node_update_signal> on_scene_update =
        [this] (wf::scene::root_node_update_signal *event)
    {
        const auto flags = event->flags & (wf::scene::update_flag::CHILDREN_LIST |
            wf::scene::update_flag::ENABLED);
        if (flags)
        {
            wf::scene::node_update_signal update{this, flags};
            this->emit(&update);
        }
    };

    windows_only_workspace_node_t(wf::output_t *output, wf::point_t ws) : node_t(false)
    {
        this->output = output;
        this->workspace = ws;
        wf::get_core().scene()->connect(&on_scene_update);
    }
    
    void gen_render_instances(std::vector<wf::scene::render_instance_uptr>& instances,
        wf::scene::damage_callback push_damage, wf::output_t *shown_on) override
    {
        if (shown_on != output)
        {
            return;
        }
        
        // Render instances must be front-to-back, matching the live scenegraph.
        // get_views() without WSET_SORT_STACKING returns insertion order.
        const auto views = output->wset()->get_views(wf::WSET_SORT_STACKING |
            wf::WSET_MAPPED_ONLY | wf::WSET_EXCLUDE_MINIMIZED);
        for (const auto& view : views)
        {
            // Retain instances for every mapped window. Scheduling clips to
            // the face's target, including portions crossing workspace edges.
            // Filtering here would also miss a window moved across an edge
            // after these persistent instances were generated.
            view->get_root_node()->gen_render_instances(instances, push_damage, shown_on);
        }
    }

    wf::geometry_t get_bounding_box() override
    {
        auto box = output->get_layout_geometry();
        const auto current = output->wset()->get_current_workspace();
        box.x += (workspace.x - current.x) * box.width;
        box.y += (workspace.y - current.y) * box.height;
        return box;
    }
};

// Custom node that shows only desktop/background (no windows)
class desktop_only_workspace_node_t : public wf::scene::node_t
{
    wf::output_t *output;
    wf::point_t workspace;
    std::shared_ptr<wf::workspace_stream_node_t> full_workspace;
    
  public:
    desktop_only_workspace_node_t(wf::output_t *output, wf::point_t ws, bool popout) : node_t(false)
    {
        this->output = output;
        this->workspace = ws;
        if (!popout)
        {
            // Keep wallpaper, windows and their blur transformers in one pass.
            full_workspace = std::make_shared<wf::workspace_stream_node_t>(output, ws);
        }
    }
    
    void gen_render_instances(std::vector<wf::scene::render_instance_uptr>& instances,
        wf::scene::damage_callback push_damage, wf::output_t *shown_on) override
    {
        if (shown_on != output)
        {
            return;
        }
        
        if (full_workspace)
        {
            full_workspace->gen_render_instances(instances, push_damage, shown_on);
            return;
        }

        // We want to render everything EXCEPT window views
        // This means rendering background layers only
        auto root = output->node_for_layer(wf::scene::layer::BACKGROUND);
        if (root)
        {
            root->gen_render_instances(instances, push_damage, shown_on);
        }
        
        auto bottom = output->node_for_layer(wf::scene::layer::BOTTOM);
        if (bottom)
        {
            bottom->gen_render_instances(instances, push_damage, shown_on);
        }
    }
    
    wf::geometry_t get_bounding_box() override
    {
        return full_workspace ? full_workspace->get_bounding_box() : output->get_layout_geometry();
    }
};

class cube_render_instance_t : public wf::scene::render_instance_t
    {
        std::shared_ptr<cube_render_node_t> self;
        wf::scene::damage_callback push_damage;

        std::vector<std::vector<wf::scene::render_instance_uptr>> ws_instances;
        std::vector<cube_damage_region_t> ws_damage;
        std::vector<wf::auxilliary_buffer_t> framebuffers;

        popout_compositor_t popout_compositor;

        // Framebuffers for window-only cubes
        std::vector<wf::auxilliary_buffer_t> framebuffers_windows;
        std::vector<std::vector<wf::auxilliary_buffer_t>> framebuffers_windows_rows;
        
        // Continuous window updates
        std::vector<std::unique_ptr<wf::scene::render_instance_manager_t>> ws_instance_managers_windows;
        std::vector<std::vector<std::unique_ptr<wf::scene::render_instance_manager_t>>> ws_instance_managers_windows_rows;

        // Multiple cube workspaces for all rows
        std::vector<std::vector<std::vector<wf::scene::render_instance_uptr>>> ws_instances_rows;
        std::vector<std::vector<cube_damage_region_t>> ws_damage_rows;
        std::vector<std::vector<wf::auxilliary_buffer_t>> framebuffers_rows;

        std::vector<cube_damage_region_t> ws_damage_windows;
        std::vector<std::vector<cube_damage_region_t>> ws_damage_windows_rows;

        bool workspace_instances_dirty = false;
        wf::signal::connection_t<wf::scene::root_node_update_signal> on_scene_update =
            [this] (wf::scene::root_node_update_signal *event)
        {
            if (event->flags & (wf::scene::update_flag::CHILDREN_LIST |
                wf::scene::update_flag::ENABLED))
            {
                workspace_instances_dirty = true;
                push_damage(self->get_bounding_box());
            }
        };

        void refresh_workspace_instances()
        {
            auto refresh = [this] (auto& nodes, auto& instances, auto& damage)
            {
                for (size_t i = 0; i < nodes.size(); ++i)
                {
                    instances[i].clear();
                    nodes[i]->gen_render_instances(instances[i],
                        [this, &damage, i] (const cube_damage_region_t& region)
                        {
                            damage[i] |= region;
                            push_damage(self->get_bounding_box());
                        }, self->cube->output);
                    damage[i] |= nodes[i]->get_bounding_box();
                    auto visible = cube_damage_region_t{nodes[i]->get_bounding_box()};
                    wf::scene::compute_visibility_from_list(instances[i], self->cube->output, visible, {0, 0});
                }
            };
            refresh(self->workspaces, ws_instances, ws_damage);
            for (size_t row = 0; row < ws_instances_rows.size(); ++row)
            {
                refresh(self->workspaces_all_rows[row], ws_instances_rows[row], ws_damage_rows[row]);
            }
            workspace_instances_dirty = false;
        }

        wf::signal::connection_t<wf::scene::node_damage_signal> on_cube_damage =
            [=] (wf::scene::node_damage_signal *ev)
        {
            push_damage(ev->region);
        };

      public:
        // FIXED: Constructor - No allocations here; just resize vectors and set pending flags
        // This avoids segfault on invalid geometry during init
        cube_render_instance_t(cube_render_node_t *self, wf::scene::damage_callback push_damage)
        {
            this->self = std::dynamic_pointer_cast<cube_render_node_t>(self->shared_from_this());
            this->push_damage = push_damage;
            self->connect(&on_cube_damage);
            wf::get_core().scene()->connect(&on_scene_update);
            
            ws_damage.resize(self->workspaces.size());
            framebuffers.resize(self->workspaces.size());
            ws_instances.resize(self->workspaces.size());
            
            // Initialize storage for all rows
            int num_rows = self->workspaces_all_rows.size();
            ws_damage_rows.resize(num_rows);
            framebuffers_rows.resize(num_rows);
            ws_instances_rows.resize(num_rows);
            
            // IMPORTANT: Resize window storage BEFORE creating managers
            ws_damage_windows.resize(self->workspaces_windows.size());
            framebuffers_windows.resize(self->workspaces_windows.size());
            ws_instance_managers_windows.resize(self->workspaces_windows.size());
            
            ws_damage_windows_rows.resize(num_rows);
            framebuffers_windows_rows.resize(num_rows);
            ws_instance_managers_windows_rows.resize(num_rows);
            
            for (int row = 0; row < num_rows; row++)
            {
                ws_damage_windows_rows[row].resize(self->workspaces_windows_rows[row].size());
                framebuffers_windows_rows[row].resize(self->workspaces_windows_rows[row].size());
                ws_instance_managers_windows_rows[row].resize(self->workspaces_windows_rows[row].size());
            }
            
    // Initialize top cube workspaces (current row)
    for (int i = 0; i < (int)self->workspaces.size(); i++)
    {
        auto push_damage_child = [=] (const cube_damage_region_t& damage)
        {
            ws_damage[i] |= damage;
            push_damage(self->get_bounding_box());
        };
        
        self->workspaces[i]->gen_render_instances(ws_instances[i],
            push_damage_child, self->cube->output);
        
        ws_damage[i] |= self->workspaces[i]->get_bounding_box();
    }
    
    // NOW create window managers after everything is resized
    for (int i = 0; i < (int)self->workspaces_windows.size(); i++)
    {
        auto push_damage_child = [this, i] (const cube_damage_region_t& damage)
        {
            this->ws_damage_windows[i] |= damage;
            this->push_damage(this->self->get_bounding_box());
        };
        
        std::vector<wf::scene::node_ptr> nodes;
        nodes.push_back(self->workspaces_windows[i]);
        
        ws_instance_managers_windows[i] = std::make_unique<wf::scene::render_instance_manager_t>(
            nodes, push_damage_child, self->cube->output);
        
        const int BIG_NUMBER = 1e5;
        cube_damage_region_t big_region = wf::geometry_t{-BIG_NUMBER, -BIG_NUMBER, 2 * BIG_NUMBER, 2 * BIG_NUMBER};
        ws_instance_managers_windows[i]->set_visibility_region(big_region);
        
        ws_damage_windows[i] |= self->workspaces_windows[i]->get_bounding_box();
    }
    
    // Initialize all other row workspaces
    for (int row = 0; row < num_rows; row++)
    {
        ws_damage_rows[row].resize(self->workspaces_all_rows[row].size());
        framebuffers_rows[row].resize(self->workspaces_all_rows[row].size());
        ws_instances_rows[row].resize(self->workspaces_all_rows[row].size());
        
        for (int i = 0; i < (int)self->workspaces_all_rows[row].size(); i++)
        {
            auto push_damage_child = [=] (const cube_damage_region_t& damage)
            {
                ws_damage_rows[row][i] |= damage;
                push_damage(self->get_bounding_box());
            };
            
            self->workspaces_all_rows[row][i]->gen_render_instances(ws_instances_rows[row][i],
                push_damage_child, self->cube->output);
            
            ws_damage_rows[row][i] |= self->workspaces_all_rows[row][i]->get_bounding_box();
        }
        
        // Create window managers for this row
        for (int i = 0; i < (int)self->workspaces_windows_rows[row].size(); i++)
        {
            auto push_damage_child = [this, row, i] (const cube_damage_region_t& damage)
            {
                this->ws_damage_windows_rows[row][i] |= damage;
                this->push_damage(this->self->get_bounding_box());
            };
            
            std::vector<wf::scene::node_ptr> nodes;
            nodes.push_back(self->workspaces_windows_rows[row][i]);
            
            ws_instance_managers_windows_rows[row][i] = 
                std::make_unique<wf::scene::render_instance_manager_t>(
                    nodes, push_damage_child, self->cube->output);
            
            const int BIG_NUMBER = 1e5;
            cube_damage_region_t big_region = wf::geometry_t{-BIG_NUMBER, -BIG_NUMBER, 2 * BIG_NUMBER, 2 * BIG_NUMBER};
            ws_instance_managers_windows_rows[row][i]->set_visibility_region(big_region);
            
            ws_damage_windows_rows[row][i] |= 
                self->workspaces_windows_rows[row][i]->get_bounding_box();
        }
    }
}

   ~cube_render_instance_t()
        {
            // Explicitly free buffers on destruction
            for (auto& fb : framebuffers) fb.free();
            for (auto& row : framebuffers_rows) {
                for (auto& fb : row) fb.free();
            }
            for (auto& fb : framebuffers_windows) fb.free();
            for (auto& row : framebuffers_windows_rows) {
                for (auto& fb : row) fb.free();
            }
        }

void schedule_instructions(
    std::vector<wf::scene::render_instruction_t>& instructions,
    const wf::render_target_t& target, cube_damage_region_t& damage) override
{
    if (workspace_instances_dirty)
    {
        refresh_workspace_instances();
    }

    if (self->cube->popout_active)
    {
        popout_compositor.begin_frame(self->cube->output);
    }

    const auto bbox = self->get_bounding_box();
    // Capture our damage before subtracting our coverage for nodes below us.
    instructions.push_back(wf::scene::render_instruction_t{
        .instance = this,
        .target = target.translated(-wf::origin(bbox)),
        .damage = damage & bbox,
    });
    damage ^= bbox;

    // Render top cube workspaces (current row) - WITH BACKGROUND
    for (int i = 0; i < (int)ws_instances.size(); i++)
    {
        const float scale = self->cube->output->handle->scale;
        auto bbox = self->workspaces[i]->get_bounding_box();
        framebuffers[i].allocate(wf::dimensions(bbox), scale);

        wf::render_target_t target{framebuffers[i]};
        target.geometry = self->workspaces[i]->get_bounding_box();
        target.scale    = self->cube->output->handle->scale;

        wf::render_pass_params_t params;
        params.instances = &ws_instances[i];
        params.damage    = ws_damage[i];
        params.reference_output = self->cube->output;
        params.target = target;
        params.flags  = wf::RPASS_CLEAR_BACKGROUND | wf::RPASS_EMIT_SIGNALS;

        wf::render_pass_t::run(params);
        ws_damage[i].clear();
    }
    
    // Render window-only workspaces dynamically (top row)
   // In schedule_instructions, for the window rendering:
// In schedule_instructions, fix the render target geometry:
// In schedule_instructions, for window rendering:
for (int i = 0; self->cube->popout_active && i < (int)ws_instance_managers_windows.size(); i++)
{
    const float scale = self->cube->output->handle->scale;
    auto bbox = self->cube->output->get_layout_geometry();
    framebuffers_windows[i].allocate(wf::dimensions(bbox), scale);

    // Calculate which workspace this represents
    auto cws = self->cube->output->wset()->get_current_workspace();
    wf::point_t target_ws = {i, 0};

    wf::render_target_t fb_target{framebuffers_windows[i]};
    fb_target.geometry = wf::geometry_t{
        bbox.x + (target_ws.x - cws.x) * bbox.width,
        bbox.y + (target_ws.y - cws.y) * bbox.height,
        bbox.width,
        bbox.height
    };
    fb_target.scale = scale;

    auto& instances = ws_instance_managers_windows[i]->get_instances();
    
    popout_compositor.render(instances, fb_target, framebuffers[i], self->cube->output);
    ws_damage_windows[i].clear();
}

    // Render all other row workspaces - WITH BACKGROUND
    for (int row = 0; row < (int)ws_instances_rows.size(); row++)
    {
        for (int i = 0; i < (int)ws_instances_rows[row].size(); i++)
        {
            const float scale = self->cube->output->handle->scale;
            auto bbox = self->workspaces_all_rows[row][i]->get_bounding_box();
            framebuffers_rows[row][i].allocate(wf::dimensions(bbox), scale);

            wf::render_target_t target{framebuffers_rows[row][i]};
            target.geometry = self->workspaces_all_rows[row][i]->get_bounding_box();
            target.scale    = self->cube->output->handle->scale;

            wf::render_pass_params_t params;
            params.instances = &ws_instances_rows[row][i];
            params.damage    = ws_damage_rows[row][i];
            params.reference_output = self->cube->output;
            params.target = target;
            params.flags  = wf::RPASS_CLEAR_BACKGROUND | wf::RPASS_EMIT_SIGNALS;

            wf::render_pass_t::run(params);
            ws_damage_rows[row][i].clear();
        }
    }
    
    // Render window-only workspaces dynamically (other rows)
for (int row = 0; self->cube->popout_active && row < (int)ws_instance_managers_windows_rows.size(); row++)
{
    for (int i = 0; i < (int)ws_instance_managers_windows_rows[row].size(); i++)
    {
        const float scale = self->cube->output->handle->scale;
        auto bbox = self->cube->output->get_layout_geometry();
        framebuffers_windows_rows[row][i].allocate(wf::dimensions(bbox), scale);

        auto cws = self->cube->output->wset()->get_current_workspace();
        int target_y = row + 1;
        wf::point_t target_ws = {i, target_y};

        wf::render_target_t fb_target{framebuffers_windows_rows[row][i]};
        fb_target.geometry = wf::geometry_t{
            bbox.x + (target_ws.x - cws.x) * bbox.width,
            bbox.y + (target_ws.y - cws.y) * bbox.height,
            bbox.width,
            bbox.height
        };
        fb_target.scale = scale;

        auto& instances = ws_instance_managers_windows_rows[row][i]->get_instances();
        
        popout_compositor.render(instances, fb_target, framebuffers_rows[row][i], self->cube->output);
        ws_damage_windows_rows[row][i].clear();
    }
}
}

void render(const wf::scene::render_instruction_t& data) override
{
    self->cube->render(data, framebuffers, framebuffers_rows, 
                      framebuffers_windows, framebuffers_windows_rows);
}

    void compute_visibility(wf::output_t *output, cube_damage_region_t& visible) override
    {
        for (size_t i = 0; i < ws_instances.size(); ++i)
        {
            auto region = cube_damage_region_t{self->workspaces[i]->get_bounding_box()};
            wf::scene::compute_visibility_from_list(ws_instances[i], output, region, {0, 0});
        }

        for (size_t row = 0; row < ws_instances_rows.size(); ++row)
        {
            for (size_t i = 0; i < ws_instances_rows[row].size(); ++i)
            {
                auto region = cube_damage_region_t{self->workspaces_all_rows[row][i]->get_bounding_box()};
                wf::scene::compute_visibility_from_list(ws_instances_rows[row][i], output, region, {0, 0});
            }
        }
        // Window-only managers maintain their own visibility. Do not render
        // here: blur's sampling and saved pixels belong to the scheduled pass.
    }

};

      public:
cube_render_node_t(wayfire_cube *cube) : node_t(false)
{
    this->cube = cube;
    auto w = cube->output->wset()->get_workspace_grid_size().width;
    auto h = cube->output->wset()->get_workspace_grid_size().height;
    const int y = 0;
    
    // Fixed top cube: workspace row zero, regardless of the active desktop.
    for (int i = 0; i < w; i++)
    {
        // CHANGED: Use desktop-only for regular cube
        auto node = std::make_shared<desktop_only_workspace_node_t>(cube->output, wf::point_t{i, y}, cube->popout_active);
        workspaces.push_back(node);
        
        // Window-only for popout cube
        auto node_windows = std::make_shared<windows_only_workspace_node_t>(cube->output, wf::point_t{i, y});
        workspaces_windows.push_back(node_windows);
    }
    
    // All other rows
    for (int row_offset = 1; row_offset < h; row_offset++)
    {
        int target_y = row_offset;
        std::vector<std::shared_ptr<wf::scene::node_t>> row_workspaces;
        std::vector<std::shared_ptr<wf::scene::node_t>> row_workspaces_windows;
        
        for (int i = 0; i < w; i++)
        {
            // CHANGED: Desktop-only for regular cube
            auto node = std::make_shared<desktop_only_workspace_node_t>(cube->output, wf::point_t{i, target_y}, cube->popout_active);
            row_workspaces.push_back(node);
            
            // Window-only for popout
            auto node_windows = std::make_shared<windows_only_workspace_node_t>(cube->output, wf::point_t{i, target_y});
            row_workspaces_windows.push_back(node_windows);
        }
        
        workspaces_all_rows.push_back(row_workspaces);
        workspaces_windows_rows.push_back(row_workspaces_windows);
    }
}

        virtual void gen_render_instances(
            std::vector<wf::scene::render_instance_uptr>& instances,
            wf::scene::damage_callback push_damage, wf::output_t *shown_on)
        {
            if (shown_on != this->cube->output)
            {
                return;
            }

            instances.push_back(std::make_unique<cube_render_instance_t>(
                this, push_damage));
        }

        wf::geometry_t get_bounding_box()
        {
            return cube->output->get_layout_geometry();
        }

private:
    std::vector<std::shared_ptr<wf::scene::node_t>> workspaces;  // Changed type
    std::vector<std::vector<std::shared_ptr<wf::scene::node_t>>> workspaces_all_rows;  // Changed type
    
    std::vector<std::shared_ptr<wf::scene::node_t>> workspaces_windows;
    std::vector<std::vector<std::shared_ptr<wf::scene::node_t>>> workspaces_windows_rows;
    
    wayfire_cube *cube;
    };

    std::unique_ptr<wf::input_grab_t> input_grab;
    std::shared_ptr<cube_render_node_t> render_node;

    wf::option_wrapper_t<double> XVelocity{"vertical_kube/speed_spin_horiz"},
    YVelocity{"vertical_kube/speed_spin_vert"}, ZVelocity{"vertical_kube/speed_zoom"};
    wf::option_wrapper_t<double> zoom_opt{"vertical_kube/zoom"};
    wf::option_wrapper_t<double> desktop_opacity{"vertical_kube/desktop_opacity"};
    wf::option_wrapper_t<bool> desktop_blur{"vertical_kube/desktop_blur"};
    wf::option_wrapper_t<bool> cap_blur{"vertical_kube/cap_blur"};
    std::unique_ptr<wf_blur_base> glass_blur;
    std::string glass_blur_method;
    wf::auxilliary_buffer_t glass_layer;
    bool popout_active = false;
    bool opening_frame_pending = false;
    bool depth_buffer_requested = false;
    scroll_momentum_t scroll_momentum;
    wf::option_wrapper_t<bool> enable_window_popout{"vertical_kube/enable_window_popout"};
    wf::option_wrapper_t<double> popout_scale{"vertical_kube/popout_scale"};  // e.g., 1.15 = 15% larger
    wf::option_wrapper_t<double> popout_opacity{"vertical_kube/popout_opacity"};  // 0.0 to 1.0
    int cap_geometry_sides = 0;
    int cap_geometry_segments = 0;
    std::vector<GLfloat> cap_vertices, cap_uvs;
    OpenGL::program_t cap_program;  // Separate program for caps
    wf::option_wrapper_t<bool> enable_caps{"vertical_kube/enable_caps"};
    wf::option_wrapper_t<double> cap_alpha{"vertical_kube/cap_alpha"};
    wf::option_wrapper_t<wf::color_t> cap_color_top{"vertical_kube/cap_color_top"};
    wf::option_wrapper_t<wf::color_t> cap_color_bottom{"vertical_kube/cap_color_bottom"};
    wf::option_wrapper_t<std::string> cap_texture_top{"vertical_kube/cap_texture_top"};
    wf::option_wrapper_t<std::string> cap_texture_bottom{"vertical_kube/cap_texture_bottom"};
    


    // Cap textures/buffers
    wf::auxilliary_buffer_t top_cap_buffer;
    wf::auxilliary_buffer_t bottom_cap_buffer;
    std::optional<wf::color_t> last_top_cap_color, last_bottom_cap_color;
    
    // Loaded cap texture images
    GLuint top_cap_texture_id = 0;
    GLuint bottom_cap_texture_id = 0;

    /* the Z camera distance so that (-1, 1) is mapped to the whole screen
     * for the given FOV */
    float identity_z_offset;

    // Camera vertical position for viewing different cube rows
    wf::animation::simple_animation_t camera_y_offset{wf::create_option<int>(300)};

    OpenGL::program_t program;

    wf_cube_animation_attribs animation;
    wf::option_wrapper_t<bool> dynamic_lighting{"vertical_kube/dynamic_lighting"};
    wf::option_wrapper_t<double> lighting_strength{"vertical_kube/lighting_strength"};
    wf::option_wrapper_t<double> lighting_speed{"vertical_kube/lighting_speed"};
    float lighting_clock = 0.0f;
    std::chrono::steady_clock::time_point lighting_last = std::chrono::steady_clock::now();

    void set_lighting(OpenGL::program_t& shader, float row)
    {
        shader.uniform1i("dynamic_lighting", dynamic_lighting);
        shader.uniform1f("lighting_time", lighting_clock);
        shader.uniform1f("lighting_row", row);
        float radius = std::max(identity_z_offset, 0.1f);
        if (tessellation_support && int(use_deform) > 0)
        {
            float factor = int(use_deform) == 1 ? 0.5f / radius : radius / 0.5f;
            radius *= std::pow(factor, float(animation.cube_animation.ease_deformation));
        }
        shader.uniform1f("lighting_radius", radius);
        shader.uniform1f("lighting_strength", float(lighting_strength) *
            std::clamp(float(animation.cube_animation.ease_deformation), 0.0f, 1.0f));
    }

    wf::option_wrapper_t<bool> use_light{"vertical_kube/light"};
    wf::option_wrapper_t<int> use_deform{"vertical_kube/deform"};

    std::string last_background_mode;
    std::unique_ptr<wf_cube_background_base> background;

    wf::option_wrapper_t<std::string> background_mode{"vertical_kube/background_mode"};

    void reload_background()
    {
        if (background && last_background_mode == (std::string)background_mode)
        {
            return;
        }

        last_background_mode = background_mode;

        if (last_background_mode == "shader")
        {
            background = std::make_unique<wf_cube_shader_background>();
        } else if (last_background_mode == "image")
        {
            background = std::make_unique<wf_cube_image_background>();
        } else if (last_background_mode == "simple")
        {
            background = std::make_unique<wf_cube_simple_background>();
        } else if (last_background_mode == "skydome")
        {
            background = std::make_unique<wf_cube_background_skydome>(output);
        } else if (last_background_mode == "cubemap")
        {
            background = std::make_unique<wf_cube_background_cubemap>();
        } else
        {
            LOGE("cube: Unrecognized background mode %s. Using default \"simple\"",
                last_background_mode.c_str());
            background = std::make_unique<wf_cube_simple_background>();
        }
    }

    bool tessellation_support;

    int get_num_faces()
    {
        return output->wset()->get_workspace_grid_size().width;
    }

    wf::plugin_activation_data_t grab_interface{
        .name = "cube",
        .capabilities = wf::CAPABILITY_MANAGE_COMPOSITOR,
        .cancel = [=] () { deactivate(); },
    };

  public:
    void init() override
    {
        input_grab = std::make_unique<wf::input_grab_t>("cube", output, nullptr, this, nullptr);
        input_grab->set_wants_raw_input(true);

        animation.cube_animation.offset_y.set(0, 0);
        animation.cube_animation.offset_z.set(0, 0);
        animation.cube_animation.rotation.set(0, 0);
        animation.cube_animation.zoom.set(1, 1);
        animation.cube_animation.ease_deformation.set(0, 0);

        animation.cube_animation.start();
        
        camera_y_offset.set(0, 0);
        popout_scale_animation.set(1.0, 1.0); 

        reload_background();

        output->connect(&on_cube_control);
        wf::gles::run_in_context([&]
        {
            load_program();
        });
    }

    void handle_pointer_button(const wlr_pointer_button_event& event) override
    {
        if (event.state == WL_POINTER_BUTTON_STATE_RELEASED)
        {
            input_ungrabbed();
        }
    }

    void handle_pointer_axis(const wlr_pointer_axis_event& event) override
    {
        if (event.orientation == WL_POINTER_AXIS_VERTICAL_SCROLL)
        {
            double amount = event.delta;
            if (amount == 0.0 && event.source == WL_POINTER_AXIS_SOURCE_WHEEL)
            {
                // wlroots expresses discrete wheel movement in v120 units.
                amount = event.delta_discrete / 8.0;
            }
            if (event.source == WL_POINTER_AXIS_SOURCE_FINGER)
            {
                amount = scroll_momentum.update(amount, event.time_msec);
            } else
            {
                scroll_momentum.reset();
            }
            pointer_scrolled(amount);
        }
    }

void load_program()
{
#ifdef VERTICAL_KUBE_USE_GLES32
    std::string ext_string(reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS)));
    tessellation_support = ext_string.find(std::string("GL_EXT_tessellation_shader")) !=
        std::string::npos;
#else
    tessellation_support = false;
#endif

    if (!tessellation_support)
    {
        program.set_simple(OpenGL::compile_program(cube_vertex_2_0, cube_fragment_2_0));
        // Load cap program in non-tessellation mode
        cap_program.set_simple(OpenGL::compile_program(cube_cap_vertex, cube_cap_fragment));
    } else
    {
#ifdef VERTICAL_KUBE_USE_GLES32
        auto id = GL_CALL(glCreateProgram());
        GLuint vss, fss, tcs, tes, gss;

        vss = OpenGL::compile_shader(cube_vertex_3_2, GL_VERTEX_SHADER);
        fss = OpenGL::compile_shader(cube_fragment_3_2, GL_FRAGMENT_SHADER);
        tcs = OpenGL::compile_shader(cube_tcs_3_2, GL_TESS_CONTROL_SHADER);
        tes = OpenGL::compile_shader(cube_tes_3_2, GL_TESS_EVALUATION_SHADER);
        gss = OpenGL::compile_shader(cube_geometry_3_2, GL_GEOMETRY_SHADER);

        GL_CALL(glAttachShader(id, vss));
        GL_CALL(glAttachShader(id, tcs));
        GL_CALL(glAttachShader(id, tes));
        GL_CALL(glAttachShader(id, gss));
        GL_CALL(glAttachShader(id, fss));

        GL_CALL(glLinkProgram(id));
        GL_CALL(glUseProgram(id));

        GL_CALL(glDeleteShader(vss));
        GL_CALL(glDeleteShader(fss));
        GL_CALL(glDeleteShader(tcs));
        GL_CALL(glDeleteShader(tes));
        GL_CALL(glDeleteShader(gss));
        
        program.set_simple(id);
        cap_program.set_simple(OpenGL::compile_program(cube_cap_vertex, cube_cap_fragment));
#endif
    }

    animation.projection = glm::perspective(45.0f, 1.f, 0.1f, 100.f);
}





    wf::signal::connection_t<cube_control_signal> on_cube_control = [=] (cube_control_signal *d)
    {
        rotate_and_zoom_cube(d->angle, d->zoom, d->ease, d->last_frame);
        d->carried_out = true;
    };

    void rotate_and_zoom_cube(double angle, double zoom, double ease,
        bool last_frame)
    {
        if (last_frame)
        {
            deactivate();

            return;
        }

        if (!activate())
        {
            return;
        }

        float offset_z = identity_z_offset + Z_OFFSET_NEAR;

        animation.cube_animation.rotation.set(angle, angle);
        animation.cube_animation.zoom.set(zoom, zoom);
        animation.cube_animation.ease_deformation.set(ease, ease);

        animation.cube_animation.offset_y.set(0, 0);
        animation.cube_animation.offset_z.set(offset_z, offset_z);

        animation.cube_animation.start();
        update_view_matrix();
        output->render->schedule_redraw();
    }

    /* Tries to initialize renderer, activate plugin, etc. */
    bool activate(bool interactive = true)
    {
        if (output->is_plugin_active(grab_interface.name))
        {
            return true;
        }

        if (!output->activate_plugin(&grab_interface))
        {
            return false;
        }

        wf::get_core().connect(&on_motion_event);

        scroll_momentum.reset();
        // Start from the current desktop, not workspace (0, 0).
        popout_active = interactive && enable_window_popout;
        opening_frame_pending = true;
        animation.in_exit = !interactive;
        animation.cube_animation.rotation.set(0, 0);
        animation.cube_animation.zoom.set(1, 1);
        animation.cube_animation.offset_y.set(0, 0);
        animation.cube_animation.ease_deformation.set(0, 0);
        const float initial_camera = output->wset()->get_current_workspace().y * CUBE_VERTICAL_SPACING;
        camera_y_offset.set(initial_camera, initial_camera);

        render_node = std::make_shared<cube_render_node_t>(this);
        wf::scene::add_front(wf::get_core().scene(), render_node);
        output->render->add_effect(&pre_hook, wf::OUTPUT_EFFECT_PRE);
        // Keep one request across cube activations. Some Wayfire versions
        // retain the cached dimensions after deleting the depth texture, so
        // releasing/reacquiring it at the same size loses depth testing.
        if (!depth_buffer_requested)
        {
            output->render->set_require_depth_buffer(true);
            depth_buffer_requested = true;
        }

        wf::get_core().hide_cursor();
        input_grab->grab_input(wf::scene::layer::OVERLAY);

        auto wsize = output->wset()->get_workspace_grid_size();
        animation.side_angle = 2 * M_PI / float(wsize.width);
        identity_z_offset    = 0.5 / std::tan(animation.side_angle / 2);
        if (wsize.width == 1)
        {
            // tan(M_PI) is 0, so identity_z_offset is invalid
            identity_z_offset = 0.0f;
        }

        reload_background();
        animation.cube_animation.offset_z.set(identity_z_offset + Z_OFFSET_NEAR,
            identity_z_offset + Z_OFFSET_NEAR);
        
        if (popout_active)
        {
            popout_scale_animation.animate(1.0, popout_scale);
        } else
        {
            popout_scale_animation.set(1.0, 1.0);
        }
        // Force a full redraw to clear any stale state
        output->render->damage_whole();
        
        return true;
    }

    int calculate_viewport_dx_from_rotation()
    {
        float dx = -animation.cube_animation.rotation / animation.side_angle;

        return std::floor(dx + 0.5);
    }

// Add this method to calculate which row we're currently viewing
int calculate_viewport_dy_from_camera()
{
    // Calculate which cube row the camera is focused on
    // Each row is offset by CUBE_VERTICAL_SPACING
    float dy = -camera_y_offset / (-CUBE_VERTICAL_SPACING);
    return std::floor(dy + 0.5);
}

// Modified deactivate() method
void deactivate()
{
    if (!output->is_plugin_active(grab_interface.name))
    {
        return;
    }

    scroll_momentum.reset();
    opening_frame_pending = false;
    wf::scene::remove_child(render_node);
    render_node = nullptr;
    output->render->rem_effect(&pre_hook);
    // No render target is bound during teardown. The normal render pass
    // clears its own buffers; clearing here can hit an incomplete framebuffer.
    output->render->damage_whole();

    input_grab->ungrab_input();
    output->deactivate_plugin(&grab_interface);
    wf::get_core().unhide_cursor();
    on_motion_event.disconnect();

    /* Figure out how much we have rotated and switch workspace */
    int size = get_num_faces();
    int dvx  = calculate_viewport_dx_from_rotation();
    
    // NEW: Calculate vertical workspace change based on camera position
    int dvy = calculate_viewport_dy_from_camera();

    auto cws = output->wset()->get_current_workspace();
    auto grid = output->wset()->get_workspace_grid_size();
    
    int nvx = (cws.x + (dvx % size) + size) % size;
    int nvy = dvy; // Camera identifies an absolute row, not an offset from cws.
    
    // Clamp to valid workspace range
    nvy = std::max(0, std::min(nvy, grid.height - 1));
    
    output->wset()->set_workspace({nvx, nvy});



    /* We are finished with rotation, make sure the next time cube is used
     * it is properly reset */
  //  animation.cube_animation.rotation.set(0, 0);
 //   camera_y_offset.set(0, 0);  // Reset camera position
}

// Navigate fixed workspace rows, queuing repeats from the pending row.
bool move_vp_vertical(int dir)
{
    if (!output->is_plugin_active(grab_interface.name) && !input_grabbed_active())
    {
        return false;
    }
    const int rows = output->wset()->get_workspace_grid_size().height;
    const int current_row = std::clamp(int(std::lround(camera_y_offset.end /
        CUBE_VERTICAL_SPACING)), 0, rows - 1);
    const int target_row = std::clamp(current_row + dir, 0, rows - 1);
    if (target_row == current_row)
    {
        return false;
    }
    animation.in_exit = false;
    camera_y_offset.animate(target_row * CUBE_VERTICAL_SPACING);
    output->render->schedule_redraw();
    return true;
}

// input_grabbed() intentionally returns false to propagate the initiating
// button; check plugin activation separately for keyboard row navigation.
bool input_grabbed_active()
{
    input_grabbed();
    return output->is_plugin_active(grab_interface.name);
}

// Modified reset_attribs to maintain camera position during transitions
void reset_attribs()
{
    animation.cube_animation.zoom.restart_with_end(1.0);
    animation.cube_animation.offset_z.restart_with_end(
        identity_z_offset + Z_OFFSET_NEAR);
    animation.cube_animation.offset_y.restart_with_end(0);
    animation.cube_animation.ease_deformation.restart_with_end(0);
    // Don't reset camera_y_offset here - let it maintain position until deactivate
}
    /* Start moving to a workspace to the left/right using the keyboard */
    bool move_vp(int dir)
    {
        const bool was_active = output->is_plugin_active(grab_interface.name);
        const bool keep_open = was_active && !animation.in_exit;
        if (!activate(false))
        {
            return false;
        }

        animation.in_exit = !keep_open;
        if (keep_open)
        {
            // Keep the interactive cube's zoom, tilt, deformation and popout.
            // Rebase every transition before restarting the shared duration.
            animation.cube_animation.zoom.restart_with_end(animation.cube_animation.zoom.end);
            animation.cube_animation.offset_z.restart_with_end(animation.cube_animation.offset_z.end);
            animation.cube_animation.offset_y.restart_with_end(animation.cube_animation.offset_y.end);
            animation.cube_animation.ease_deformation.restart_with_end(
                animation.cube_animation.ease_deformation.end);
        } else
        {
            // Desktop shortcuts animate a complete workspace on the cube face,
            // without creating a separate floating window layer.
            reset_attribs();
        }

        // Queue repeated keys from the pending destination, snapping mouse
        // rotation to a face before advancing one face in the chosen direction.
        const float face = std::round(animation.cube_animation.rotation.end / animation.side_angle);
        animation.cube_animation.rotation.restart_with_end((face - dir) * animation.side_angle);
        animation.cube_animation.start();
        update_view_matrix();
        output->render->schedule_redraw();
        return true;
    }

    /* Initiate with an button grab. */
    bool input_grabbed()
    {
        if (!activate())
        {
            return false;
        }

        if (!popout_active && enable_window_popout)
        {
            popout_active = true;
            wf::scene::remove_child(render_node);
            render_node = std::make_shared<cube_render_node_t>(this);
            wf::scene::add_front(wf::get_core().scene(), render_node);
            popout_scale_animation.animate(1.0, popout_scale);
        } else if (animation.in_exit && popout_active)
        {
            // Re-grabbing a closing cube resumes from its visible scale.
            popout_scale_animation.animate(popout_scale);
        }

        /* Rotations, offset_y and zoom stay as they are now, as they have been
         * grabbed.
         * offset_z changes to the default one.
         *
         * We also need to make sure the cube gets deformed */
        animation.in_exit = false;
        float current_rotation = animation.cube_animation.rotation;
        float current_offset_y = animation.cube_animation.offset_y;
        float current_zoom     = animation.cube_animation.zoom;

        animation.cube_animation.rotation.set(current_rotation, current_rotation);
        animation.cube_animation.offset_y.set(current_offset_y, current_offset_y);
        animation.cube_animation.offset_z.restart_with_end(
            zoom_opt + identity_z_offset + Z_OFFSET_NEAR);

        animation.cube_animation.zoom.set(current_zoom, current_zoom);
        animation.cube_animation.ease_deformation.restart_with_end(1);

        animation.cube_animation.start();

        update_view_matrix();
        output->render->schedule_redraw();

        // Let the button go to the input grab
        return false;
    }

    /* Mouse grab was released */
    void input_ungrabbed()
    {
        scroll_momentum.reset();
        animation.in_exit = true;

        /* Rotate cube so that selected workspace aligns with the output */
        float current_rotation = animation.cube_animation.rotation;
        int dvx = calculate_viewport_dx_from_rotation();
        animation.cube_animation.rotation.set(current_rotation,
            -dvx * animation.side_angle);
        /* And reset other attributes, again to align the workspace with the output
         * */
        reset_attribs();

        popout_scale_animation.animate(1.0);
        animation.cube_animation.start();

        update_view_matrix();
        output->render->schedule_redraw();
    }

    /* Update the view matrix used in the next frame */
    void update_view_matrix()
    {
        auto zoom_translate = glm::translate(glm::mat4(1.f),
            glm::vec3(0.f, 0.f, -animation.cube_animation.offset_z));

        auto rotation = glm::rotate(glm::mat4(1.0),
            (float)animation.cube_animation.offset_y,
            glm::vec3(1., 0., 0.));

        // Apply camera vertical offset for viewing different cube rows
        auto camera_vertical = glm::translate(glm::mat4(1.0),
            glm::vec3(0.f, camera_y_offset, 0.f));

        auto view = glm::lookAt(glm::vec3(0., 0., 0.),
            glm::vec3(0., 0., -animation.cube_animation.offset_z),
            glm::vec3(0., 1., 0.));

        animation.view = zoom_translate * rotation * camera_vertical * view;
    }

    glm::mat4 output_transform(const wf::render_target_t& target)
    {
        auto scale = glm::scale(glm::mat4(1.0), {1, -1, 1});
        return wf::gles::render_target_gl_to_framebuffer(target) * scale;
    }

glm::mat4 calculate_vp_matrix(const wf::render_target_t& dest)
{
    float zoom_factor = animation.cube_animation.zoom;
    
    // NEW: Build scale with row-centered translation wrapper
    auto scale_matrix = glm::scale(glm::mat4(1.0),
        glm::vec3(1. / zoom_factor, 1. / zoom_factor, 1. / zoom_factor));

    // NEW: Translate scene so current row is at Y=0 for zoom, scale, then translate back
    auto to_row_center = glm::translate(glm::mat4(1.0), glm::vec3(0.0f, camera_y_offset, 0.0f));
    auto from_row_center = glm::translate(glm::mat4(1.0), glm::vec3(0.0f, -camera_y_offset, 0.0f));
    auto centered_scale = from_row_center * scale_matrix * to_row_center;

    // Compose: projection * view * centered_scale (applies row-focused zoom)
    return output_transform(dest) * animation.projection * animation.view * centered_scale;
}

    /* Calculate the base model matrix for the i-th side of the cube */
glm::mat4 calculate_model_matrix(int i, float vertical_offset = 0.0f, float scale = 1.0f)
{
    const float angle =
        i * animation.side_angle + animation.cube_animation.rotation;
    auto rotation = glm::rotate(glm::mat4(1.0), angle, glm::vec3(0, 1, 0));
    
    double additional_z = 0.0;
    if (get_num_faces() == 2)
    {
        additional_z = 1e-3;
    }
    
    // Translation without vertical offset (just Z position)
    auto translation = glm::translate(glm::mat4(1.0),
        glm::vec3(0, 0, identity_z_offset + additional_z));
    
    // Apply uniform scaling to everything
    auto scale_matrix = glm::scale(glm::mat4(1.0), glm::vec3(scale, scale, scale));
    
    // Apply vertical offset AFTER scaling so it doesn't get scaled
    auto vertical_translation = glm::translate(glm::mat4(1.0),
        glm::vec3(0, vertical_offset, 0));
    
    // Order: rotate, scale (including Z position), then add vertical offset
    return vertical_translation * rotation * scale_matrix * translation;
}
// Sample each face edge at the same subdivision count as the
// side tessellation shader. Deforming just polygon corners cannot form a
// circular or star-shaped cap. Keep this mesh cached across animation frames.
int cap_segments() const
{
    return tessellation_support && int(use_deform) > 0 ? (int(use_light) > 0 ? 50 : 30) : 1;
}

std::vector<GLfloat> generate_cap_vertices(int num_sides)
{
    std::vector<GLfloat> vertices{0.0f, 0.0f};
    const int segments = cap_segments();
    const float z = identity_z_offset + (num_sides == 2 ? 1e-3f : 0.0f);
    for (int face = 0; face < num_sides; ++face)
    {
        const float angle = face * animation.side_angle;
        for (int step = 0; step < segments; ++step)
        {
            const float x = -0.5f + float(step) / segments;
            vertices.push_back(std::cos(angle) * x + std::sin(angle) * z);
            vertices.push_back(-std::sin(angle) * x + std::cos(angle) * z);
        }
    }
    vertices.push_back(vertices[2]);
    vertices.push_back(vertices[3]);
    return vertices;
}

std::vector<GLfloat> generate_cap_uvs(int num_sides)
{
    std::vector<GLfloat> uvs;
    const float radius = std::hypot(identity_z_offset, 0.5f);
    for (float coordinate : cap_vertices)
    {
        uvs.push_back(0.5f + coordinate / (2.0f * radius));
    }
    return uvs;
}

void render_cap(bool is_top, float vertical_offset, const wf::render_target_t& target, bool depth_only, bool translucent_only, float opacity,
    const std::vector<glm::vec3>& polygon = {})
{
  if (!enable_caps)
        return;
        
    int num_sides = get_num_faces();
    if (cap_geometry_sides != num_sides || cap_geometry_segments != cap_segments())
    {
        cap_vertices = generate_cap_vertices(num_sides);
        cap_uvs = generate_cap_uvs(num_sides);
        cap_geometry_sides = num_sides;
        cap_geometry_segments = cap_segments();
    }
    
    if (cap_program.get_program_id(wf::TEXTURE_TYPE_RGBA) == 0)
    {
        cap_program.set_simple(OpenGL::compile_program(cube_cap_vertex, cube_cap_fragment));
    }
    
    cap_program.use(wf::TEXTURE_TYPE_RGBA);
    cap_program.attrib_pointer("position", 2, 0, cap_vertices.data());
    cap_program.attrib_pointer("uvPosition", 2, 0, cap_uvs.data());
    
    float y_pos = vertical_offset;
    // No depth offset at all
    
    glm::mat4 model(1.0f);
    model = glm::rotate(model, (float)animation.cube_animation.rotation, glm::vec3(0.0f, 1.0f, 0.0f));
    model = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, y_pos, 0.0f)) * model;
    
    auto vp = calculate_vp_matrix(target);
    cap_program.uniformMatrix4f("VP", vp);
    cap_program.uniformMatrix4f("model", model);
    std::vector<GLfloat> piece_vertices, piece_uvs;
    if (!polygon.empty())
    {
        const auto inverse_model = glm::inverse(model);
        const float diameter = 2.0f * std::hypot(identity_z_offset, 0.5f);
        for (auto p : polygon)
        {
            auto local = inverse_model * glm::vec4(p, 1);
            piece_vertices.insert(piece_vertices.end(), {local.x, local.z});
            piece_uvs.insert(piece_uvs.end(), {0.5f + local.x / diameter, 0.5f + local.z / diameter});
        }
        cap_program.attrib_pointer("position", 2, 0, piece_vertices.data());
        cap_program.attrib_pointer("uvPosition", 2, 0, piece_uvs.data());
    }
    cap_program.uniform1f("cap_alpha", opacity);
    cap_program.uniform1i("depth_only", depth_only);
    cap_program.uniform1i("translucent_only", translucent_only);
    cap_program.uniform1i("deform", tessellation_support ? int(use_deform) : 0);
    cap_program.uniform1f("ease", animation.cube_animation.ease_deformation);
    
    static auto start_time = std::chrono::steady_clock::now();
    auto current_time = std::chrono::steady_clock::now();
    float elapsed = std::chrono::duration<float>(current_time - start_time).count();
    cap_program.uniform1f("time", elapsed);
    set_lighting(cap_program, vertical_offset + (is_top ? -0.5f : 0.5f));

    cap_program.uniform1f("u_num_sides", (float)num_sides);
    cap_program.uniform1f("cap_apothem", identity_z_offset);
    cap_program.uniform1f("cap_uv_radius", std::hypot(identity_z_offset, 0.5f));
    
    auto& buffer = is_top ? top_cap_buffer : bottom_cap_buffer;
    auto tex_id = wf::gles_texture_t::from_aux(buffer).tex_id;
    GL_CALL(glBindTexture(GL_TEXTURE_2D, tex_id));
    
    GL_CALL(glDisable(GL_CULL_FACE));
    GL_CALL(glDrawArrays(GL_TRIANGLE_FAN, 0,
        polygon.empty() ? cap_vertices.size() / 2 : piece_vertices.size() / 2));
    
    cap_program.deactivate();
}


    
    // Render cap textures inside the GLES subpass
void render_cap_textures()
{
    if (!enable_caps)
    {
        return;
    }

    auto update = [] (wf::auxilliary_buffer_t& buffer, std::optional<wf::color_t>& previous,
        const wf::color_t& color)
    {
        if (previous && *previous == color)
        {
            return;
        }
        // The cap shader applies its lighting/mask procedurally. Its base
        // texture is a uniform color, so one texel produces the same image.
        buffer.allocate({1, 1});
        wf::gles::bind_render_buffer(buffer.get_renderbuffer());
        GL_CALL(glDisable(GL_SCISSOR_TEST));
        OpenGL::clear({color.r, color.g, color.b, color.a});
        previous = color;
    };
    update(top_cap_buffer, last_top_cap_color, cap_color_top);
    update(bottom_cap_buffer, last_bottom_cap_color, cap_color_bottom);
}


    bool prepare_opening_frame()
    {
        if (!opening_frame_pending) return false;
        opening_frame_pending = false;
        // Texture capture and shader preparation can outlast an animation.
        // Start both clocks only when the first cube frame is ready to draw.
        animation.cube_animation.start();
        popout_scale_animation.wf::animation::duration_t::start();
        update_view_matrix();
        return true;
    }

    void render(const wf::scene::render_instruction_t& data, 
                std::vector<wf::auxilliary_buffer_t>& buffers, 
                std::vector<std::vector<wf::auxilliary_buffer_t>>& buffers_rows,
                std::vector<wf::auxilliary_buffer_t>& buffers_windows,
                std::vector<std::vector<wf::auxilliary_buffer_t>>& buffers_windows_rows)
    {
        data.pass->custom_gles_subpass([&]
        {
            if (program.get_program_id(wf::TEXTURE_TYPE_RGBA) == 0)
            {
                load_program();
            }

            // Wayfire 0.11 only guarantees a GL context inside the subpass.
            // Cap rendering binds offscreen targets; restore the output afterwards.
            render_cap_textures();
            wf::gles::bind_render_buffer(data.target);

            GL_CALL(glClearColor(0.0f, 0.0f, 0.0f, 1.0f));
            GL_CALL(glEnable(GL_DEPTH_TEST));
            GL_CALL(glDepthFunc(GL_LESS));
            GL_CALL(glDepthMask(GL_TRUE));
            GL_CALL(glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT));

            reload_background();
            GL_CALL(glDisable(GL_DEPTH_TEST));
            GL_CALL(glDisable(GL_CULL_FACE));
            GL_CALL(glDisable(GL_BLEND));
            background->render_frame(data.target, animation);

            GL_CALL(glEnable(GL_DEPTH_TEST));
            GL_CALL(glDepthMask(GL_TRUE));
            GL_CALL(glDepthFunc(GL_LESS));
            GL_CALL(glClear(GL_DEPTH_BUFFER_BIT));

            const bool first_opening_frame = prepare_opening_frame();
            const float frame_popout_scale = first_opening_frame ? 1.0f : float(popout_scale_animation);
            auto vp = calculate_vp_matrix(data.target);
            program.use(wf::TEXTURE_TYPE_RGBA);

            static GLfloat vertexData[] = {
                -0.5, 0.5,
                0.5, 0.5,
                0.5, -0.5,
                -0.5, -0.5
            };

            static GLfloat coordData[] = {
                0.0f, 1.0f,
                1.0f, 1.0f,
                1.0f, 0.0f,
                0.0f, 0.0f
            };

            auto opacity_at_camera = [&] (float configured)
            {
                return cube_glass_opacity(configured,
                    animation.cube_animation.ease_deformation,
                    animation.cube_animation.zoom, animation.cube_animation.offset_z,
                    identity_z_offset + Z_OFFSET_NEAR, zoom_opt);
            };
            const float face_opacity = opacity_at_camera(desktop_opacity);
            const float window_opacity = opacity_at_camera(popout_opacity);
            const float cap_opacity = opacity_at_camera(cap_alpha);

            struct surface_t
            {
                wf::auxilliary_buffer_t *buffer;
                int face;
                float y, scale, opacity, depth;
                bool cap, top, blur, window_layer;
                std::vector<glm::vec3> polygon;
            };
            std::vector<surface_t> surfaces;
            const auto cws = output->wset()->get_current_workspace();
            auto depth_at = [&] (glm::vec4 point)
            {
                auto projected = vp * point;
                return projected.z / projected.w;
            };
            auto add_faces = [&] (auto& textures, float y, float scale, float opacity, bool blur, bool window_layer)
            {
                for (int face = 0; face < get_num_faces(); ++face)
                {
                    auto center = calculate_model_matrix(face, y, scale) * glm::vec4(0, 0, 0, 1);
                    if (tessellation_support && int(use_deform) > 0)
                    {
                        float d = std::max(glm::length(glm::vec2(center.x, center.z)), 1e-6f);
                        float factor = int(use_deform) == 1 ? 0.5f * scale / d : d / (0.5f * scale);
                        factor = std::pow(factor, float(animation.cube_animation.ease_deformation));
                        center.x *= factor;
                        center.z *= factor;
                    }
                    surfaces.push_back({&textures[(cws.x + face) % get_num_faces()], face,
                        y, scale, opacity, depth_at(center), false, false, blur, window_layer});
                }
            };
            for (int row = 0; row <= int(buffers_rows.size()); ++row)
            {
                float y = -row * CUBE_VERTICAL_SPACING;
                add_faces(row ? buffers_rows[row - 1] : buffers, y, 1.0f,
                    face_opacity, desktop_blur, false);
                if (popout_active)
                    add_faces(row ? buffers_windows_rows[row - 1] : buffers_windows,
                        y, frame_popout_scale, window_opacity, false, true);
                if (enable_caps)
                    for (bool top : {false, true})
                    {
                        float cap_y = y + (top ? 0.5f : -0.5f);
                        surfaces.push_back({nullptr, 0, cap_y, 1.0f, cap_opacity,
                            depth_at(glm::vec4(0, cap_y, 0, 1)), true, top, bool(cap_blur), false});
                    }
            }
            // Recover the eye in cube coordinates, including row-centered zoom.
            const auto homogeneous_eye = glm::inverse(vp) * glm::vec4(0, 0, 1, 0);
            const float eye_y = homogeneous_eye.y / homogeneous_eye.w;
            const glm::vec3 eye = glm::vec3(homogeneous_eye) / homogeneous_eye.w;
            std::vector<cube_surface_bounds_t> bounds;
            const bool planar = !tessellation_support || int(use_deform) == 0 ||
                float(animation.cube_animation.ease_deformation) == 0.0f;
            for (const auto& surface : surfaces)
            {
                const float half_height = surface.cap ? 0.0f : surface.scale * 0.5f;
                bounds.push_back({surface.y - half_height, surface.y + half_height,
                    surface.depth, surface.cap});
                auto& bound = bounds.back();
                if (planar)
                {
                    if (surface.cap)
                    {
                        for (int face = 0; face < get_num_faces(); ++face)
                            bound.outline.push_back(glm::vec3(calculate_model_matrix(face,
                                surface.y) * glm::vec4(-0.5f, 0, 0, 1)));
                        bound.plane = glm::vec4(0, 1, 0, -surface.y);
                    } else
                    {
                        const auto model = calculate_model_matrix(surface.face, surface.y, surface.scale);
                        for (int corner = 0; corner < 4; ++corner)
                            bound.outline.push_back(glm::vec3(model * glm::vec4(
                                vertexData[2 * corner], vertexData[2 * corner + 1], 0, 1)));
                        const auto normal = glm::normalize(glm::vec3(model * glm::vec4(0, 0, 1, 0)));
                        bound.plane = glm::vec4(normal, -glm::dot(normal, bound.outline[0]));
                    }
                }
            }
            auto unordered_surfaces = std::move(surfaces);
            surfaces.clear();
            surfaces.reserve(unordered_surfaces.size());
            if (planar)
            {
                for (auto& piece : cube_planar_order(bounds, eye))
                {
                    surfaces.push_back(unordered_surfaces[piece.surface]);
                    surfaces.back().polygon = std::move(piece.outline);
                }
            } else
            {
                for (int index : cube_surface_order(bounds, eye_y))
                    surfaces.push_back(unordered_surfaces[index]);
            }
            auto draw = [&] (const surface_t& surface, bool depth_only, bool translucent_only = false)
            {
                GL_CALL(glEnable(GL_DEPTH_TEST));
                GL_CALL(glDepthFunc(GL_LEQUAL));
                GL_CALL(glDepthMask(depth_only ? GL_TRUE : GL_FALSE));
                GL_CALL(glDisable(GL_CULL_FACE)); // Draw the interior as well as the exterior.
                GL_CALL(glActiveTexture(GL_TEXTURE0));
                if (surface.cap)
                {
                    render_cap(surface.top, surface.y, data.target, depth_only, translucent_only,
                        surface.opacity, surface.polygon);
                    return;
                }
                program.use(wf::TEXTURE_TYPE_RGBA);
                program.attrib_pointer("position", 2, 0, vertexData);
                program.attrib_pointer("uvPosition", 2, 0, coordData);
                std::vector<GLfloat> piece_vertices, piece_uvs;
                if (!surface.polygon.empty())
                {
                    const auto inverse_model = glm::inverse(calculate_model_matrix(
                        surface.face, surface.y, surface.scale));
                    for (size_t i = 1; i + 1 < surface.polygon.size(); ++i)
                        for (size_t corner : {size_t(0), i, i + 1})
                        {
                            const auto p = inverse_model * glm::vec4(surface.polygon[corner], 1);
                            piece_vertices.insert(piece_vertices.end(), {p.x, p.y});
                            piece_uvs.insert(piece_uvs.end(), {p.x + 0.5f, p.y + 0.5f});
                        }
                    program.attrib_pointer("position", 2, 0, piece_vertices.data());
                    program.attrib_pointer("uvPosition", 2, 0, piece_uvs.data());
                }
                program.uniformMatrix4f("VP", vp);
                program.uniformMatrix4f("model", calculate_model_matrix(surface.face, surface.y, surface.scale));
                program.uniform1f("surface_opacity", surface.opacity);
                program.uniform1i("depth_only", depth_only);
                program.uniform1i("translucent_only", translucent_only);
                program.uniform1i("smp", 0);
                set_lighting(program, surface.y);
                if (tessellation_support)
                {
                    program.uniform1i("window_layer", surface.window_layer);
                    program.uniform1i("deform", use_deform);
                    program.uniform1i("light", use_light);
                    program.uniform1f("ease", animation.cube_animation.ease_deformation);
                    program.uniform1f("popout_scale", surface.scale);
                    program.uniform1f("cameraYOffset", camera_y_offset);
                    program.uniform1f("cubeVerticalOffset", surface.y);
                }
                GL_CALL(glBindTexture(GL_TEXTURE_2D, wf::gles_texture_t::from_aux(*surface.buffer).tex_id));
                static const GLuint indices[] = {0, 1, 2, 0, 2, 3};
#ifdef VERTICAL_KUBE_USE_GLES32
                if (tessellation_support)
                {
                    if (surface.polygon.empty())
                    {
                        GL_CALL(glDrawElements(GL_PATCHES, 6, GL_UNSIGNED_INT, indices));
                    }
                    else
                    {
                        GL_CALL(glDrawArrays(GL_PATCHES, 0, piece_vertices.size() / 2));
                    }
                }
                else
#endif
                {
                    if (surface.polygon.empty())
                    {
                        GL_CALL(glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, indices));
                    }
                    else
                    {
                        GL_CALL(glDrawArrays(GL_TRIANGLES, 0, piece_vertices.size() / 2));
                    }
                }
                program.deactivate();
            };

            // Build the backdrop in drawing order. A global depth-only pass
            // leaves unpainted holes under future windows which native blur
            // can sample before those windows have supplied their colors.
            if (desktop_blur || cap_blur)
            {
                const auto option = wf::get_core().config->get_option<std::string>("blur/method");
                if (option && (!glass_blur || glass_blur_method != option->get_value()))
                {
                    glass_blur_method = option->get_value();
                    glass_blur = create_blur_from_name(glass_blur_method);
                }
            } else
            {
                glass_blur.reset();
                glass_layer.free();
            }
            for (const auto& surface : surfaces)
            {
                if (surface.opacity <= 0.0f) continue;
                wf::gles::bind_render_buffer(data.target);
                GL_CALL(glEnable(GL_BLEND));
                GL_CALL(glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA));
                if (!surface.blur || !glass_blur || surface.opacity >= 1.0f)
                {
                    if (surface.opacity >= 0.999f)
                    {
                        // Opaque color and its depth become valid together.
                        GL_CALL(glDisable(GL_BLEND));
                        draw(surface, true);
                    }
                    GL_CALL(glEnable(GL_BLEND));
                    GL_CALL(glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA));
                    // Disjoint alpha tests mean every pixel is blended once.
                    draw(surface, false, true);
                    continue;
                }
                // Render this glass surface into a transparent screen-sized
                // layer, sharing depth from already-painted surfaces with the main target.
                GLint depth_type = 0, depth_name = 0;
                GL_CALL(glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                    GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &depth_type));
                GL_CALL(glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                    GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &depth_name));
                glass_layer.allocate(data.target.get_size());
                wf::gles::bind_render_buffer(glass_layer.get_renderbuffer());
                if (depth_type == GL_TEXTURE)
                {
                    GL_CALL(glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depth_name, 0));
                }
                else if (depth_type == GL_RENDERBUFFER)
                {
                    GL_CALL(glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth_name));
                }
                GL_CALL(glDisable(GL_SCISSOR_TEST));
                OpenGL::clear({0, 0, 0, 0});
                GL_CALL(glDisable(GL_BLEND));
                draw(surface, false);
                if (depth_type == GL_TEXTURE)
                {
                    GL_CALL(glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, 0, 0));
                }
                else if (depth_type == GL_RENDERBUFFER)
                {
                    GL_CALL(glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, 0));
                }
                GL_CALL(glDisable(GL_DEPTH_TEST));
                glass_blur->prepare_blur(data.target, cube_damage_region_t{data.target.geometry});
                GL_CALL(glEnable(GL_BLEND));
                GL_CALL(glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA));
                glass_blur->render(wf::gles_texture_t::from_aux(glass_layer), data.target.geometry,
                    cube_damage_region_t{data.target.geometry}, data.target, data.target);
            }
            wf::gles::bind_render_buffer(data.target);
            GL_CALL(glDepthMask(GL_TRUE));
            GL_CALL(glDisable(GL_BLEND));
            GL_CALL(glDisable(GL_CULL_FACE));
            GL_CALL(glDisable(GL_DEPTH_TEST));
            program.deactivate();
        });
    }


wf::effect_hook_t pre_hook = [=] ()
{
    const auto now = std::chrono::steady_clock::now();
    const float dt = std::chrono::duration<float>(now - lighting_last).count();
    lighting_last = now;
    if (dynamic_lighting)
        lighting_clock += std::min(dt, 0.1f) * float(lighting_speed);
    update_view_matrix();
    wf::scene::damage_node(render_node, render_node->get_bounding_box());
    
    if (animation.cube_animation.running() || camera_y_offset.running() || popout_scale_animation.running())
    {
        output->render->schedule_redraw();
    } else if (animation.in_exit)
    {
        deactivate();
    } else if ((std::string)background_mode == "shader" || enable_caps || dynamic_lighting)
    {
        // Keep procedural backgrounds and cap animation moving while held still.
        output->render->schedule_redraw();
    }
};

    wf::signal::connection_t<wf::input_event_signal<wlr_pointer_motion_event>> on_motion_event =
        [=] (wf::input_event_signal<wlr_pointer_motion_event> *ev)
    {
        pointer_moved(ev->event);

        ev->event->delta_x    = 0;
        ev->event->delta_y    = 0;
        ev->event->unaccel_dx = 0;
        ev->event->unaccel_dy = 0;
    };

    void pointer_moved(wlr_pointer_motion_event *ev)
    {
        if (animation.in_exit)
        {
            return;
        }

        double xdiff = ev->delta_x;
        double ydiff = ev->delta_y * -1.0;

        animation.cube_animation.zoom.restart_with_end(
            animation.cube_animation.zoom.end);

        double current_off_y = animation.cube_animation.offset_y;
        double off_y = current_off_y + ydiff * YVelocity;

        off_y = wf::clamp(off_y, -1.5, 1.5);
        animation.cube_animation.offset_y.set(current_off_y, off_y);
        animation.cube_animation.offset_z.restart_with_end(
            animation.cube_animation.offset_z.end);

        float current_rotation = animation.cube_animation.rotation;
        animation.cube_animation.rotation.restart_with_end(
            current_rotation + xdiff * XVelocity);

        animation.cube_animation.ease_deformation.restart_with_end(
            animation.cube_animation.ease_deformation.end);

        animation.cube_animation.start();
        output->render->schedule_redraw();
    }


// Accumulate toward the pending zoom target so high-resolution touchpad
// events and wheel ticks are not lost while the animation is still running.
void pointer_scrolled(double amount)
{
    if (animation.in_exit || !std::isfinite(amount) || amount == 0.0)
    {
        return;
    }

    animation.cube_animation.offset_y.restart_with_end(
        animation.cube_animation.offset_y.end);
    animation.cube_animation.rotation.restart_with_end(
        animation.cube_animation.rotation.end);
    animation.cube_animation.ease_deformation.restart_with_end(
        animation.cube_animation.ease_deformation.end);
    // Preserve the entry distance and the selected row's zoom focal point.
    animation.cube_animation.offset_z.restart_with_end(
        animation.cube_animation.offset_z.end);

    const float start_zoom = animation.cube_animation.zoom;
    const double step = std::clamp(amount * double(ZVelocity) * 0.1, -20.0, 20.0);
    const float target_zoom = std::clamp(
        double(animation.cube_animation.zoom.end) * std::exp(step),
        double(ZOOM_MIN), double(ZOOM_MAX));
    animation.cube_animation.zoom.set(start_zoom, target_zoom);
    animation.cube_animation.start();
    output->render->schedule_redraw();
}


    void fini() override
    {
        if (output->is_plugin_active(grab_interface.name))
        {
            deactivate();
        }

        if (depth_buffer_requested)
        {
            output->render->set_require_depth_buffer(false);
            depth_buffer_requested = false;
        }

        wf::gles::run_in_context_if_gles([&]
        {
            glass_blur.reset();
            glass_layer.free();
            program.free_resources();
            cap_program.free_resources();
            background.reset();
            
            if (top_cap_texture_id)
            {
                GL_CALL(glDeleteTextures(1, &top_cap_texture_id));
            }
            if (bottom_cap_texture_id)
            {
                GL_CALL(glDeleteTextures(1, &bottom_cap_texture_id));
            }
                
            top_cap_buffer.free();
            bottom_cap_buffer.free();
            last_top_cap_color.reset();
            last_bottom_cap_color.reset();
        });
    }
};


class WayfireVerticalKube : public wf::plugin_interface_t,
    public wf::per_output_tracker_mixin_t<wayfire_cube>
{
    wf::ipc_activator_t rotate_left{"vertical_kube/rotate_left"};
    wf::ipc_activator_t rotate_right{"vertical_kube/rotate_right"};
    wf::ipc_activator_t rotate_up{"vertical_kube/rotate_up"};
    wf::ipc_activator_t rotate_down{"vertical_kube/rotate_down"};
    wf::ipc_activator_t activate{"vertical_kube/activate"};

  public:
    void init() override
    {
        if (!wf::get_core().is_gles2())
        {
            const char *render_type =
                wf::get_core().is_vulkan() ? "vulkan" : (wf::get_core().is_pixman() ? "pixman" : "unknown");
            LOGE("cube: requires GLES2 support, but current renderer is ", render_type);
            return;
        }

        this->init_output_tracking();
        rotate_left.set_handler(rotate_left_cb);
        rotate_right.set_handler(rotate_right_cb);
        rotate_up.set_handler(rotate_up_cb);
        rotate_down.set_handler(rotate_down_cb);
        activate.set_handler(activate_cb);
    }

    void fini() override
    {
        this->fini_output_tracking();
    }

    wf::ipc_activator_t::handler_t rotate_left_cb = [=] (wf::output_t *output, wayfire_view)
    {
        return this->output_instance[output]->move_vp(-1);
    };

    wf::ipc_activator_t::handler_t rotate_right_cb = [=] (wf::output_t *output, wayfire_view)
    {
        return this->output_instance[output]->move_vp(+1);
    };

    wf::ipc_activator_t::handler_t rotate_up_cb = [=] (wf::output_t *output, wayfire_view)
    {
        return this->output_instance[output]->move_vp_vertical(-1);
    };

    wf::ipc_activator_t::handler_t rotate_down_cb = [=] (wf::output_t *output, wayfire_view)
    {
        return this->output_instance[output]->move_vp_vertical(+1);
    };

    wf::ipc_activator_t::handler_t activate_cb = [=] (wf::output_t *output, wayfire_view)
    {
        return this->output_instance[output]->input_grabbed();
    };
};

DECLARE_WAYFIRE_PLUGIN(WayfireVerticalKube);
