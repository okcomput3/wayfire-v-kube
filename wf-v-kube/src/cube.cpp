#include <wayfire/per-output-plugin.hpp>
#include <memory>
#include <wayfire/plugin.hpp>
#include <wayfire/opengl.hpp>
#include <wayfire/output.hpp>
#include <wayfire/core.hpp>
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
#include "cube-control-signal.hpp"
#include "wayfire/region.hpp"
#include "wayfire/scene-render.hpp"
#include "wayfire/scene.hpp"
#include "wayfire/signal-definitions.hpp"
#include <chrono>

#define Z_OFFSET_NEAR 0.89567f
#define Z_OFFSET_FAR  2.00000f

#define ZOOM_MAX 10.0f
#define ZOOM_MIN 0.1f


#define CUBE_VERTICAL_SPACING -1.2f

#ifdef USE_GLES32
    #include <GLES3/gl32.h>
#endif

#include "shaders.tpp"
#include "shaders-3-2.tpp"

// Vertex shader - updated to pass world position
static const char *cube_cap_vertex = R"(
#version 100
attribute mediump vec2 position;
attribute mediump vec2 uvPosition;

uniform mat4 VP;
uniform mat4 model;

varying mediump vec2 uvpos;
varying mediump vec3 worldPos;

void main() {
    uvpos = uvPosition;
    vec4 worldPosition = model * vec4(position.x, 0.0, position.y, 1.0);
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
uniform float time;
uniform float u_num_sides;

// Simple rounded corner mask for square UV space
float roundedCornerMask(vec2 uv, float radius) {
    vec2 centered_uv = uv - 0.5;
    vec2 corner_dist = abs(centered_uv) - (0.5 - radius);
    
    if (corner_dist.x <= 0.0 && corner_dist.y <= 0.0) {
        return 1.0;
    }
    
    if (corner_dist.x > 0.0 && corner_dist.y > 0.0) {
        float dist_to_corner = length(corner_dist);
        return smoothstep(radius + 0.01, radius - 0.01, dist_to_corner);
    }
    
    return 1.0;
}

// Edge anti-aliasing
float edgeAntiAliasMask(vec2 uv) {
    float min_edge_dist = min(min(uv.x, 1.0 - uv.x), min(uv.y, 1.0 - uv.y));
    float aa_width = 0.01;
    return smoothstep(0.0, aa_width, min_edge_dist);
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
    
    // Apply simple rounded corners (works in UV space)
    float corner_radius = 0.6;
    float corner_mask = roundedCornerMask(uvpos, corner_radius);
    float edge_mask = edgeAntiAliasMask(uvpos);
    float final_mask = min(corner_mask, edge_mask);
    
    // Combine texture alpha with corner mask
    float finalAlpha = texColor.a * final_mask;
    
    gl_FragColor = vec4(finalColor, finalAlpha);
}
)";

// Background Vertex Shader - Simple fullscreen quad
static const char *background_vertex_shader = R"(
#version 100
attribute vec2 position;
varying vec2 v_uv;

void main() {
    gl_Position = vec4(position, 0.0, 1.0);
    v_uv = position * 0.5 + 0.5;
}
)";

// Background Fragment Shader - Volumetric space/nebula
static const char *background_fragment_shader = R"(
#version 100
precision mediump float;

uniform float u_time;
uniform vec2 u_resolution;
varying vec2 v_uv;

#define iterations 4
#define formuparam2 0.89
#define volsteps 10
#define stepsize 0.190
#define zoom 3.900
#define tile 0.450
#define speed2 0.010
#define brightness 0.2
#define darkmatter 0.400
#define distfading 0.560
#define saturation 0.400
#define transverseSpeed 1.1
#define cloud 0.2

float field(in vec3 p, float u_time) {
    float strength = 7.0 + 0.03 * log(1.e-6 + fract(sin(u_time) * 4373.11));
    float accum = 0.;
    float prev = 0.;
    float tw = 0.;

    for (int i = 0; i < 6; ++i) {
        float mag = dot(p, p);
        p = abs(p) / mag + vec3(-0.5, -0.8 + 0.1 * sin(u_time * 0.2 + 2.0), -1.1 + 0.3 * cos(u_time * 0.15));
        float w = exp(-float(i) / 7.0);
        accum += w * exp(-strength * pow(abs(mag - prev), 2.3));
        tw += w;
        prev = mag;
    }
    return max(0.0, 5.0 * accum / tw - 0.7);
}

void main() {
    vec2 iResolution = u_resolution;
    float iTime = u_time / 3.0;
    
    vec2 fragCoord = v_uv * iResolution;
    vec2 uv2 = 2.0 * fragCoord.xy / iResolution.xy - 1.0;
    vec2 uvs = uv2 * iResolution.xy / max(iResolution.x, iResolution.y);

    float time2 = iTime;
    float speed = 0.005 * cos(time2 * 0.02 + 3.1415926 / 4.0);
    float formuparam = formuparam2;
    
    vec2 uv = uvs;
    float a_xz = 0.9;
    float a_yz = -0.6;
    float a_xy = 0.9 + iTime * 0.04;

    mat2 rot_xz = mat2(cos(a_xz), sin(a_xz), -sin(a_xz), cos(a_xz));
    mat2 rot_yz = mat2(cos(a_yz), sin(a_yz), -sin(a_yz), cos(a_yz));
    mat2 rot_xy = mat2(cos(a_xy), sin(a_xy), -sin(a_xy), cos(a_xy));

    vec3 dir = vec3(uv * zoom, 1.0);
    vec3 from = vec3(0.0, 0.0, 0.0);

    from.x -= 2.5;
    from.y -= 2.5;

    vec3 forward = vec3(0.0, 0.0, 1.0);

    from.x += transverseSpeed * cos(0.01 * iTime) + 0.001 * iTime;
    from.y += transverseSpeed * sin(0.01 * iTime) + 0.001 * iTime;
    from.z += 0.003 * iTime;

    dir.xy *= rot_xy;
    forward.xy *= rot_xy;
    dir.xz *= rot_xz;
    forward.xz *= rot_xz;
    dir.yz *= rot_yz;
    forward.yz *= rot_yz;

    from.xy *= -rot_xy;
    from.xz *= rot_xz;
    from.yz *= rot_yz;

    float zooom = (time2 - 3311.0) * speed;
    from += forward * zooom;
    float sampleShift = mod(zooom, stepsize);
    float zoffset = -sampleShift;
    sampleShift /= stepsize;

    float s = 0.24;
    float s3 = s + stepsize / 2.0;
    vec3 v = vec3(0.0);
    float t3 = 0.0;

    vec3 backCol2 = vec3(0.0);
    for (int r = 0; r < volsteps; r++) {
        vec3 p2 = from + (s + zoffset) * dir;
        vec3 p3 = (from + (s3 + zoffset) * dir) * (1.9 / zoom);

        p2 = abs(vec3(tile) - mod(p2, vec3(tile * 2.0)));
        p3 = abs(vec3(tile) - mod(p3, vec3(tile * 2.0)));

        t3 = field(p3, u_time);

        float pa, a = pa = 0.0;
        for (int i = 0; i < iterations; i++) {
            p2 = abs(p2) / dot(p2, p2) - formuparam;
            float D = abs(length(p2) - pa);
            
            if (i > 2) {
                a += i > 7 ? min(12.0, D) : D;
            }
            pa = length(p2);
        }

        a *= a * a;
        float s1 = s + zoffset;
        float fade = pow(distfading, max(0.0, float(r) - sampleShift));

        v += fade;

        if (r == 0)
            fade *= (1.0 - sampleShift);
        if (r == volsteps - 1)
            fade *= sampleShift;
            
        v += vec3(s1, s1 * s1, s1 * s1 * s1 * s1) * a * brightness * fade;
        backCol2 += vec3(0.20 * t3 * t3 * t3, 0.4 * t3 * t3, t3 * 0.7) * fade;

        s += stepsize;
        s3 += stepsize;
    }

    v = mix(vec3(length(v)), v, saturation);
    vec4 forCol2 = vec4(v * 0.01, 1.0);
    backCol2 *= cloud;

    gl_FragColor = forCol2 + vec4(backCol2 * 0.6, 1.0);
}
)";

class wayfire_cube : public wf::per_output_plugin_instance_t, public wf::pointer_interaction_t
{
      wf::animation::simple_animation_t popout_scale_animation{wf::create_option<int>(300)}; // 0.3 second
    class cube_render_node_t : public wf::scene::node_t
    {

// Custom node that filters to show only windows (no background/desktop)
class windows_only_workspace_node_t : public wf::scene::node_t
{
    wf::output_t *output;
    wf::point_t workspace;
    
  public:
    windows_only_workspace_node_t(wf::output_t *output, wf::point_t ws) : node_t(false)
    {
        this->output = output;
        this->workspace = ws;
    }
    
    void gen_render_instances(std::vector<wf::scene::render_instance_uptr>& instances,
        wf::scene::damage_callback push_damage, wf::output_t *shown_on) override
    {
        if (shown_on != output)
        {
            return;
        }
        
        // Get all views and filter by workspace
        auto views = output->wset()->get_views();
        
        int view_count = 0;
        for (auto& view : views)
        {
            if (!view->is_mapped())
            {
                continue;
            }
            
            // Check if view is on our workspace
            auto view_ws = output->wset()->get_view_main_workspace(view);
            if (view_ws != workspace)
            {
                continue;
            }
            
            view_count++;
            LOGI("Generating render instances for view on workspace ", workspace.x, ",", workspace.y);
            
            // Use root node which includes decorations
            auto view_node = view->get_root_node();
            if (view_node)
            {
                size_t before = instances.size();
                view_node->gen_render_instances(instances, push_damage, shown_on);
                LOGI("Generated ", instances.size() - before, " render instances");
            }
        }
        
        LOGI("Total views on workspace ", workspace.x, ",", workspace.y, ": ", view_count);
    }
    
    wf::geometry_t get_bounding_box() override
    {
        return output->get_layout_geometry();
    }
};

// Custom node that shows only desktop/background (no windows)
class desktop_only_workspace_node_t : public wf::scene::node_t
{
    wf::output_t *output;
    wf::point_t workspace;
    
  public:
    desktop_only_workspace_node_t(wf::output_t *output, wf::point_t ws) : node_t(false)
    {
        this->output = output;
        this->workspace = ws;
    }
    
    void gen_render_instances(std::vector<wf::scene::render_instance_uptr>& instances,
        wf::scene::damage_callback push_damage, wf::output_t *shown_on) override
    {
        if (shown_on != output)
        {
            return;
        }
        
        // Get the workspace scene graph
        auto wset = output->wset();
        auto views = wset->get_views();
        
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
        return output->get_layout_geometry();
    }
};

class cube_render_instance_t : public wf::scene::render_instance_t
    {
        std::shared_ptr<cube_render_node_t> self;
        wf::scene::damage_callback push_damage;

        std::vector<std::vector<wf::scene::render_instance_uptr>> ws_instances;
        std::vector<wf::region_t> ws_damage;
        std::vector<wf::auxilliary_buffer_t> framebuffers;

        // Framebuffers for window-only cubes
        std::vector<wf::auxilliary_buffer_t> framebuffers_windows;
        std::vector<std::vector<wf::auxilliary_buffer_t>> framebuffers_windows_rows;
        
        // Continuous window updates
        std::vector<std::unique_ptr<wf::scene::render_instance_manager_t>> ws_instance_managers_windows;
        std::vector<std::vector<std::unique_ptr<wf::scene::render_instance_manager_t>>> ws_instance_managers_windows_rows;

        // Multiple cube workspaces for all rows
        std::vector<std::vector<std::vector<wf::scene::render_instance_uptr>>> ws_instances_rows;
        std::vector<std::vector<wf::region_t>> ws_damage_rows;
        std::vector<std::vector<wf::auxilliary_buffer_t>> framebuffers_rows;

        std::vector<wf::region_t> ws_damage_windows;
        std::vector<std::vector<wf::region_t>> ws_damage_windows_rows;

        // FIXED: Dirty flags for conditional realloc
        std::vector<bool> window_dirty;
        std::vector<std::vector<bool>> window_rows_dirty;

        // FIXED: Pending allocation flags to defer alloc until valid geometry
        std::vector<bool> pending_alloc_desktop;
        std::vector<bool> pending_alloc_windows;
        std::vector<std::vector<bool>> pending_alloc_windows_rows;
        std::vector<std::vector<bool>> pending_alloc_desktop_rows;

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
            
            // FIXED: No allocations - set pending flags to true for all
            auto bbox = self->cube->output->get_layout_geometry();
            const float scale = self->cube->output->handle->scale;
            
            // Top row desktops - pending
            pending_alloc_desktop.assign(self->workspaces.size(), bbox.width > 0 && bbox.height > 0);
            
            // All rows desktops - pending
            pending_alloc_desktop_rows.resize(num_rows);
            for (int row = 0; row < num_rows; ++row) {
                pending_alloc_desktop_rows[row].assign(self->workspaces_all_rows[row].size(), bbox.width > 0 && bbox.height > 0);
            }
            
            // Window buffers - top row pending
            pending_alloc_windows.assign(self->workspaces_windows.size(), bbox.width > 0 && bbox.height > 0);
            
            // Window buffers - all rows pending
            pending_alloc_windows_rows.resize(num_rows);
            for (int row = 0; row < num_rows; ++row) {
                pending_alloc_windows_rows[row].assign(self->workspaces_windows_rows[row].size(), bbox.width > 0 && bbox.height > 0);
            }
            
            // FIXED: Dirty flags init
            window_dirty.assign(self->workspaces_windows.size(), false);
            window_rows_dirty.resize(num_rows);
            for (int row = 0; row < num_rows; ++row) {
                window_rows_dirty[row].assign(self->workspaces_windows_rows[row].size(), false);
            }
    
    for (int row = 0; row < num_rows; row++)
    {
        ws_damage_windows_rows[row].resize(self->workspaces_windows_rows[row].size());
        framebuffers_windows_rows[row].resize(self->workspaces_windows_rows[row].size());
        ws_instance_managers_windows_rows[row].resize(self->workspaces_windows_rows[row].size());
    }
    
    // Initialize top cube workspaces (current row)
    for (int i = 0; i < (int)self->workspaces.size(); i++)
    {
        auto push_damage_child = [=] (const wf::region_t& damage)
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
        auto push_damage_child = [this, i] (const wf::region_t& damage)
        {
            this->ws_damage_windows[i] |= damage;
            this->push_damage(this->self->get_bounding_box());
        };
        
        std::vector<wf::scene::node_ptr> nodes;
        nodes.push_back(self->workspaces_windows[i]);
        
        ws_instance_managers_windows[i] = std::make_unique<wf::scene::render_instance_manager_t>(
            nodes, push_damage_child, self->cube->output);
        
        const int BIG_NUMBER = 1e5;
        wf::region_t big_region = wf::geometry_t{-BIG_NUMBER, -BIG_NUMBER, 2 * BIG_NUMBER, 2 * BIG_NUMBER};
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
            auto push_damage_child = [=] (const wf::region_t& damage)
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
            auto push_damage_child = [this, row, i] (const wf::region_t& damage)
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
            wf::region_t big_region = wf::geometry_t{-BIG_NUMBER, -BIG_NUMBER, 2 * BIG_NUMBER, 2 * BIG_NUMBER};
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
    const wf::render_target_t& target, wf::region_t& damage) override
{

      if (self->cube->enable_caps)
    {
        self->cube->render_cap_textures();
    }
   auto bbox = self->get_bounding_box();
            damage ^= bbox;

            const float scale = self->cube->output->handle->scale;
            bool resize_dirty = false;  // TODO: Set from output resize signal

    instructions.push_back(wf::scene::render_instruction_t{
        .instance = this,
        .target   = target.translated(-wf::origin(self->get_bounding_box())),
        .damage   = damage & self->get_bounding_box(),
    });

  
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
for (int i = 0; i < (int)ws_instance_managers_windows.size(); i++)
{
    const float scale = self->cube->output->handle->scale;
    auto bbox = self->cube->output->get_layout_geometry();
    framebuffers_windows[i].allocate(wf::dimensions(bbox), scale);

    // Calculate which workspace this represents
    auto cws = self->cube->output->wset()->get_current_workspace();
    auto grid = self->cube->output->wset()->get_workspace_grid_size();
    wf::point_t target_ws = {(cws.x + i) % grid.width, cws.y};

    wf::render_target_t fb_target{framebuffers_windows[i]};
    fb_target.geometry = wf::geometry_t{
        bbox.x + target_ws.x * bbox.width,
        bbox.y + target_ws.y * bbox.height,
        bbox.width,
        bbox.height
    };
    fb_target.scale = scale;

    auto& instances = ws_instance_managers_windows[i]->get_instances();
    
    // Force full damage to ensure complete redraw
    wf::region_t full_damage = fb_target.geometry;
    
    wf::render_pass_params_t params;
    params.instances = &instances;
    params.damage = full_damage;  // Use full damage, not accumulated damage
    params.reference_output = self->cube->output;
    params.target = fb_target;
    params.flags = wf::RPASS_CLEAR_BACKGROUND | wf::RPASS_EMIT_SIGNALS;

    wf::render_pass_t::run(params);
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
for (int row = 0; row < (int)ws_instance_managers_windows_rows.size(); row++)
{
    for (int i = 0; i < (int)ws_instance_managers_windows_rows[row].size(); i++)
    {
        const float scale = self->cube->output->handle->scale;
        auto bbox = self->cube->output->get_layout_geometry();
        framebuffers_windows_rows[row][i].allocate(wf::dimensions(bbox), scale);

        auto cws = self->cube->output->wset()->get_current_workspace();
        auto grid = self->cube->output->wset()->get_workspace_grid_size();
        int target_y = (cws.y + row + 1) % grid.height;
        wf::point_t target_ws = {(cws.x + i) % grid.width, target_y};

        wf::render_target_t fb_target{framebuffers_windows_rows[row][i]};
        fb_target.geometry = wf::geometry_t{
            bbox.x + target_ws.x * bbox.width,
            bbox.y + target_ws.y * bbox.height,
            bbox.width,
            bbox.height
        };
        fb_target.scale = scale;

        auto& instances = ws_instance_managers_windows_rows[row][i]->get_instances();
        
        // Force full damage to ensure complete redraw
        wf::region_t full_damage = fb_target.geometry;
        
        wf::render_pass_params_t params;
        params.instances = &instances;
        params.damage = full_damage;  // Use full damage, not accumulated damage
        params.reference_output = self->cube->output;
        params.target = fb_target;
        params.flags = wf::RPASS_CLEAR_BACKGROUND | wf::RPASS_EMIT_SIGNALS;

        wf::render_pass_t::run(params);
        ws_damage_windows_rows[row][i].clear();
    }
}
}

    void update_cap_textures_in_schedule()
    {
        if (!self->cube->enable_caps)
            return;
            
        self->cube->render_cap_textures();
    }

// NEW: Helper to render a view to a buffer
void render_view_to_buffer(wayfire_view view, wf::auxilliary_buffer_t& buffer)
{
    auto toplevel = wf::toplevel_cast(view);
    if (!toplevel)
    {
        return; // Only works for toplevel views
    }
    
    auto vg = toplevel->get_geometry();
    buffer.allocate(wf::dimensions(vg), 1.0f);
    
    // Create render instance manager for this view
    std::vector<wf::scene::node_ptr> nodes;
    nodes.push_back(view->get_root_node());
    
    auto push_damage_dummy = [=] (wf::region_t) {};
    
    wf::scene::render_instance_manager_t instance_manager(
        nodes, push_damage_dummy, self->cube->output);
    
    instance_manager.set_visibility_region(vg);
    
    // Render the view
    wf::render_target_t target{buffer};
    target.geometry = vg;
    target.scale = 1.0f;
    
    std::vector<wf::scene::render_instance_uptr> instances;
    wf::region_t damage;
    damage |= vg;  // Add geometry to damage region
    
    for (auto& node : nodes)
    {
        node->gen_render_instances(instances, push_damage_dummy, self->cube->output);
    }
    
    wf::render_pass_params_t params;
    params.instances = &instances;
    params.damage = damage;
    params.reference_output = self->cube->output;
    params.target = target;
    params.flags = wf::RPASS_CLEAR_BACKGROUND | wf::RPASS_EMIT_SIGNALS;
    
    wf::render_pass_t::run(params);
}

void render(const wf::scene::render_instruction_t& data) override
{
    self->cube->render(data, framebuffers, framebuffers_rows, 
                      framebuffers_windows, framebuffers_windows_rows);
}

    void compute_visibility(wf::output_t *output, wf::region_t& visible) override
    {
        for (int i = 0; i < (int)self->workspaces.size(); i++)
        {
            wf::region_t ws_region = self->workspaces[i]->get_bounding_box();
            for (auto& ch : this->ws_instances[i])
            {
                ch->compute_visibility(output, ws_region);
            }
        }
        
        // NEW: Compute visibility for window-only top row
for (int i = 0; i < (int)ws_instance_managers_windows.size(); i++)
{
    wf::region_t ws_region = self->workspaces_windows[i]->get_bounding_box();
    for (auto& ch : ws_instance_managers_windows[i]->get_instances())
    {
        ch->compute_visibility(output, ws_region);
    }
}
        
       for (int row = 0; row < (int)ws_instance_managers_windows_rows.size(); row++)
{
    for (int i = 0; i < (int)ws_instance_managers_windows_rows[row].size(); i++)
    {
        wf::region_t ws_region = self->workspaces_windows_rows[row][i]->get_bounding_box();
        for (auto& ch : ws_instance_managers_windows_rows[row][i]->get_instances())
        {
            ch->compute_visibility(output, ws_region);
        }
    }
}
        
        // NEW: Compute visibility for window-only other rows
        for (int row = 0; row < (int)ws_instance_managers_windows_rows.size(); row++)
{
    for (int i = 0; i < (int)ws_instance_managers_windows_rows[row].size(); i++)
    {
        const float scale = self->cube->output->handle->scale;
        auto bbox = self->cube->output->get_layout_geometry();
        framebuffers_windows_rows[row][i].allocate(wf::dimensions(bbox), scale);

        wf::render_target_t fb_target{framebuffers_windows_rows[row][i]};
        fb_target.geometry = bbox;
        fb_target.scale = scale;

        auto& instances = ws_instance_managers_windows_rows[row][i]->get_instances();
        
        wf::render_pass_params_t params;
        params.instances = &instances;
        params.damage = ws_damage_windows_rows[row][i];
        params.reference_output = self->cube->output;
        params.target = fb_target;
        params.flags = wf::RPASS_CLEAR_BACKGROUND | wf::RPASS_EMIT_SIGNALS;

        wf::render_pass_t::run(params);
        ws_damage_windows_rows[row][i].clear();
    }
}
    }
};

      public:
cube_render_node_t(wayfire_cube *cube) : node_t(false)
{
    this->cube = cube;
    auto w = cube->output->wset()->get_workspace_grid_size().width;
    auto h = cube->output->wset()->get_workspace_grid_size().height;
    auto y = cube->output->wset()->get_current_workspace().y;
    
    // Top cube - current row
    for (int i = 0; i < w; i++)
    {
        // CHANGED: Use desktop-only for regular cube
        auto node = std::make_shared<desktop_only_workspace_node_t>(cube->output, wf::point_t{i, y});
        workspaces.push_back(node);
        
        // Window-only for popout cube
        auto node_windows = std::make_shared<windows_only_workspace_node_t>(cube->output, wf::point_t{i, y});
        workspaces_windows.push_back(node_windows);
    }
    
    // All other rows
    for (int row_offset = 1; row_offset < h; row_offset++)
    {
        int target_y = (y + row_offset) % h;
        std::vector<std::shared_ptr<wf::scene::node_t>> row_workspaces;
        std::vector<std::shared_ptr<wf::scene::node_t>> row_workspaces_windows;
        
        for (int i = 0; i < w; i++)
        {
            // CHANGED: Desktop-only for regular cube
            auto node = std::make_shared<desktop_only_workspace_node_t>(cube->output, wf::point_t{i, target_y});
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
    wf::option_wrapper_t<bool> enable_window_popout{"vertical_kube/enable_window_popout"};
    wf::option_wrapper_t<double> popout_scale{"vertical_kube/popout_scale"};  // e.g., 1.15 = 15% larger
    wf::option_wrapper_t<double> popout_opacity{"vertical_kube/popout_opacity"};  // 0.0 to 1.0
    OpenGL::program_t cap_program;  // Separate program for caps
    wf::option_wrapper_t<bool> enable_caps{"vertical_kube/enable_caps"};
    wf::option_wrapper_t<double> cap_alpha{"vertical_kube/cap_alpha"};
    wf::option_wrapper_t<wf::color_t> cap_color_top{"vertical_kube/cap_color_top"};
    wf::option_wrapper_t<wf::color_t> cap_color_bottom{"vertical_kube/cap_color_bottom"};
    wf::option_wrapper_t<std::string> cap_texture_top{"vertical_kube/cap_texture_top"};
    wf::option_wrapper_t<std::string> cap_texture_bottom{"vertical_kube/cap_texture_bottom"};
    
    OpenGL::program_t background_program;
    GLuint background_vbo = 0;


    // Cap textures/buffers
    wf::auxilliary_buffer_t top_cap_buffer;
    wf::auxilliary_buffer_t bottom_cap_buffer;
    
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
    wf::option_wrapper_t<bool> use_light{"vertical_kube/light"};
    wf::option_wrapper_t<int> use_deform{"vertical_kube/deform"};

    std::string last_background_mode;
    std::unique_ptr<wf_cube_background_base> background;

    wf::option_wrapper_t<std::string> background_mode{"vertical_kube/background_mode"};

    void reload_background()
    {
        if (last_background_mode == (std::string)background_mode)
        {
            return;
        }

        last_background_mode = background_mode;

        if (last_background_mode == "simple")
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
            pointer_scrolled(event.delta);
        }
    }

void load_program()
{
#ifdef USE_GLES32
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
#ifdef USE_GLES32
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

    // Load background shader program
    background_program.set_simple(OpenGL::compile_program(
        background_vertex_shader, background_fragment_shader));
    
    // Create fullscreen quad VBO for background
    if (background_vbo == 0)
    {
        static const GLfloat quad_vertices[] = {
            -1.0f, -1.0f,
             1.0f, -1.0f,
            -1.0f,  1.0f,
             1.0f,  1.0f
        };
        
        GL_CALL(glGenBuffers(1, &background_vbo));
        GL_CALL(glBindBuffer(GL_ARRAY_BUFFER, background_vbo));
        GL_CALL(glBufferData(GL_ARRAY_BUFFER, sizeof(quad_vertices), 
                             quad_vertices, GL_STATIC_DRAW));
        GL_CALL(glBindBuffer(GL_ARRAY_BUFFER, 0));
    }

    animation.projection = glm::perspective(45.0f, 1.f, 0.1f, 100.f);
}


void render_shader_background(const wf::render_target_t& target)
{
    if (background_program.get_program_id(wf::TEXTURE_TYPE_RGBA) == 0)
    {
        return;
    }
    
    // Render with depth test, but write max depth
    GL_CALL(glEnable(GL_DEPTH_TEST));
    GL_CALL(glDepthFunc(GL_LEQUAL));  // Use LEQUAL
    GL_CALL(glDepthMask(GL_TRUE));
    
    background_program.use(wf::TEXTURE_TYPE_RGBA);
    
    static auto start_time = std::chrono::steady_clock::now();
    auto current_time = std::chrono::steady_clock::now();
    float elapsed = std::chrono::duration<float>(current_time - start_time).count();
    background_program.uniform1f("u_time", elapsed);
    
    auto geom = output->get_layout_geometry();
    background_program.uniform2f("u_resolution", (float)geom.width, (float)geom.height);
    
    GL_CALL(glBindBuffer(GL_ARRAY_BUFFER, background_vbo));
    background_program.attrib_pointer("position", 2, 0, nullptr);
    
    GL_CALL(glDrawArrays(GL_TRIANGLE_STRIP, 0, 4));
    
    GL_CALL(glBindBuffer(GL_ARRAY_BUFFER, 0));
    background_program.deactivate();
    
    // Restore GL_LESS for cube
    GL_CALL(glDepthFunc(GL_LESS));
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
    bool activate()
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

 output->wset()->set_workspace({0, 0});

        render_node = std::make_shared<cube_render_node_t>(this);
        wf::scene::add_front(wf::get_core().scene(), render_node);
        output->render->add_effect(&pre_hook, wf::OUTPUT_EFFECT_PRE);
        output->render->set_require_depth_buffer(true);
//        output->wset()->set_workspace({0, 0});


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
        
        popout_scale_animation.animate(1.0, popout_scale);
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

    // Animate popout scale back to 1.0
  //  popout_scale_animation.animate(1.0);
    
    // Don't actually deactivate until animation finishes
   // animation.in_exit = true;

    wf::scene::remove_child(render_node);
    output->render->damage_whole();

    render_node = nullptr;
    output->render->rem_effect(&pre_hook);
  //  output->render->set_require_depth_buffer(false);

wf::gles::run_in_context([&]
{
    GL_CALL(glClear(GL_DEPTH_BUFFER_BIT));
});



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
    int nvy = (cws.y + dvy) % grid.height;
    
    // Clamp to valid workspace range
    nvy = std::max(0, std::min(nvy, grid.height - 1));
    
    output->wset()->set_workspace({nvx, nvy});



    /* We are finished with rotation, make sure the next time cube is used
     * it is properly reset */
  //  animation.cube_animation.rotation.set(0, 0);
 //   camera_y_offset.set(0, 0);  // Reset camera position
}

// Modified move_vp_vertical to track row changes more accurately
bool move_vp_vertical(int dir)
{
    bool was_active = output->is_plugin_active(grab_interface.name);
    
    if (!was_active && !activate())
    {
        return false;
    }

    // Calculate target camera position, scaled by current zoom
    float current_offset = camera_y_offset;
    float zoom_factor = animation.cube_animation.zoom;
    float effective_spacing = CUBE_VERTICAL_SPACING / zoom_factor;
    float target_offset = current_offset + (dir * effective_spacing);
    
    // Get grid height to limit movement
    auto grid = output->wset()->get_workspace_grid_size();
    int max_rows = grid.height - 1;
    
    // Clamp the camera position to valid cube range
    float min_offset = -max_rows * -CUBE_VERTICAL_SPACING;
    float max_offset = 0.0f;
    target_offset = std::max(min_offset, std::min(max_offset, target_offset));
    
    // If we're already at the target, do nothing
    if (std::abs(target_offset - current_offset) < 0.01f)
    {
        return false;
    }

    // Don't exit on movement - stay in cube mode
    animation.in_exit = false;

    // Animate camera to new position
    camera_y_offset.animate(target_offset);

    // Keep other animations stable but update offset_z for new focal point
    animation.cube_animation.zoom.restart_with_end(
        animation.cube_animation.zoom.end);
    animation.cube_animation.rotation.restart_with_end(
        animation.cube_animation.rotation.end);
    animation.cube_animation.ease_deformation.restart_with_end(
        animation.cube_animation.ease_deformation.end);
    animation.cube_animation.offset_y.restart_with_end(
        animation.cube_animation.offset_y.end);
    
    // Recalculate offset_z for new camera position
    float base_offset = identity_z_offset + Z_OFFSET_NEAR;
    float y_distance =2;
    float z_adjust = std::sqrt(base_offset * base_offset + y_distance * y_distance) - base_offset;
    animation.cube_animation.offset_z.restart_with_end(base_offset + z_adjust);

    animation.cube_animation.start();
    update_view_matrix();
    output->render->schedule_redraw();

    return true;
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
        if (!activate())
        {
            return false;
        }

        /* After the rotation is done, we want to exit cube and focus the target
         * workspace */
        animation.in_exit = true;

        /* Set up rotation target to the next workspace in the given direction,
         * and reset other attribs */
        reset_attribs();
        animation.cube_animation.rotation.restart_with_end(
            animation.cube_animation.rotation.end - dir * animation.side_angle);

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
        animation.in_exit = true;

        /* Rotate cube so that selected workspace aligns with the output */
        float current_rotation = animation.cube_animation.rotation;
        int dvx = calculate_viewport_dx_from_rotation();
        animation.cube_animation.rotation.set(current_rotation,
            -dvx * animation.side_angle);
        /* And reset other attributes, again to align the workspace with the output
         * */
        reset_attribs();

        popout_scale_animation.animate(1.01); //longer time to fix screen glitch
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
    /* Render the sides of the cube, using the given culling mode - cw or ccw */
void render_cube(GLuint front_face, std::vector<wf::auxilliary_buffer_t>& buffers, float vertical_offset = 0.0f, float scale = 1.0f)
{

    // Force depth test state at start of every cube render
    GL_CALL(glEnable(GL_DEPTH_TEST));
    GL_CALL(glDepthFunc(GL_LESS));
    GL_CALL(glDepthMask(GL_TRUE));

    // Make sure the program is still active
    GLint current_program;
    GL_CALL(glGetIntegerv(GL_CURRENT_PROGRAM, &current_program));
    if (current_program != (GLint)program.get_program_id(wf::TEXTURE_TYPE_RGBA))
    {
        program.use(wf::TEXTURE_TYPE_RGBA);
    }

    GL_CALL(glFrontFace(front_face));
    static const GLuint indexData[] = {0, 1, 2, 0, 2, 3};

    // Set the cube's vertical offset for lighting
    if (tessellation_support)
    {
        GLint loc = glGetUniformLocation(program.get_program_id(wf::TEXTURE_TYPE_RGBA), "cubeVerticalOffset");
        if (loc >= 0)
        {
            GL_CALL(glUniform1f(loc, vertical_offset));
        }
    }


 auto cws = output->wset()->get_current_workspace();
    for (int i = 0; i < get_num_faces(); i++)
    {
        int index = (cws.x + i) % get_num_faces();
        auto tex_id = wf::gles_texture_t::from_aux(buffers[index]).tex_id;
        
        // NEW: Log texture info
      //  LOGI("Binding texture ", tex_id, " for face ", i, " scale=", scale);
        
        GL_CALL(glBindTexture(GL_TEXTURE_2D, tex_id));

        auto model = calculate_model_matrix(i, vertical_offset, scale);
        program.uniformMatrix4f("model", model);

        if (tessellation_support)
        {
#ifdef USE_GLES32
            GL_CALL(glDrawElements(GL_PATCHES, 6, GL_UNSIGNED_INT, &indexData));
#endif
        } else
        {
            GL_CALL(glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT,
                &indexData));
        }
    }
}


std::vector<GLfloat> generate_cap_vertices(int num_sides)
{
    std::vector<GLfloat> vertices;
    
    // Center point
    vertices.push_back(0.0f);
    vertices.push_back(0.0f);
    
    // Calculate the proper radius
    // The cube face is a square with side length 1.0
    // From the center of the cube to the middle of a face edge is identity_z_offset
    // But we need to reach the CORNER of the face, which is further
    
    // For a cube face at distance identity_z_offset:
    // The face is 1.0 x 1.0 (from -0.5 to +0.5 in both directions)
    // The distance from center to corner of the face is:
    // sqrt(identity_z_offset^2 + 0.5^2 + 0.5^2)
    // But we're looking at it from above, so we need the horizontal distance
    
    // Actually, simpler approach:
    // At the cap position (top/bottom of cube), the cube extends from:
    // -0.5 to +0.5 in X and Z directions
    // So the cap should have radius that reaches to the corners: sqrt(0.5^2 + 0.5^2)
    // But we also need to account for identity_z_offset positioning
    
    // CORRECT calculation:
    // Each cube face is positioned at identity_z_offset from center
    // The face spans -0.5 to +0.5 in height/width
    // When looking down from top, we see a regular polygon
    // The radius should be such that the polygon edges touch the cube face edges
    
    // For a regular polygon inscribed to touch the cube:
    // radius = identity_z_offset / cos(side_angle/2)
    float cap_radius = identity_z_offset / std::cos(animation.side_angle / 2.0f);
    
    // Generate perimeter vertices
    for (int i = 0; i <= num_sides; i++)
    {
        float angle = (float)i * animation.side_angle;
        float x = cap_radius * std::sin(angle);
        float z = cap_radius * std::cos(angle);
        vertices.push_back(x);
        vertices.push_back(z);
    }
    
    return vertices;
}
    
    // Generate UV coordinates for cap
    std::vector<GLfloat> generate_cap_uvs(int num_sides)
    {
        std::vector<GLfloat> uvs;
        
        // Center UV
        uvs.push_back(0.5f);
        uvs.push_back(0.5f);
        
        // Perimeter UVs
        for (int i = 0; i <= num_sides; i++)
        {
            float angle = (float)i * animation.side_angle;
            float u = 0.5f + 0.5f * std::sin(angle);
            float v = 0.5f + 0.5f * std::cos(angle);
            uvs.push_back(u);
            uvs.push_back(v);
        }
        
        return uvs;
    }
    
    // Render a cap (top or bottom)
// The problem: Your depth offset is pushing the top cap AWAY from camera
// and bottom cap TOWARD camera, but it should be the opposite!

void render_cap(bool is_top, float vertical_offset, const wf::render_target_t& target)
{
  if (!enable_caps)
        return;
        
    int num_sides = get_num_faces();
    auto vertices = generate_cap_vertices(num_sides);
    auto uvs = generate_cap_uvs(num_sides);
    
    if (cap_program.get_program_id(wf::TEXTURE_TYPE_RGBA) == 0)
    {
        cap_program.set_simple(OpenGL::compile_program(cube_cap_vertex, cube_cap_fragment));
    }
    
    GL_CALL(glEnable(GL_BLEND));
    GL_CALL(glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));
    GL_CALL(glEnable(GL_DEPTH_TEST));
    GL_CALL(glDepthFunc(GL_LEQUAL));  // Use LEQUAL to allow equal depth values
    GL_CALL(glDepthMask(GL_TRUE));
    
    cap_program.use(wf::TEXTURE_TYPE_RGBA);
    cap_program.attrib_pointer("position", 2, 0, vertices.data());
    cap_program.attrib_pointer("uvPosition", 2, 0, uvs.data());
    
    float y_pos = vertical_offset;
    // No depth offset at all
    
    glm::mat4 model(1.0f);
    float alignment_rotation = animation.side_angle / 2.0f;
    model = glm::rotate(model, alignment_rotation, glm::vec3(0.0f, 1.0f, 0.0f));
    model = glm::rotate(model, (float)animation.cube_animation.rotation, glm::vec3(0.0f, 1.0f, 0.0f));
    model = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, y_pos, 0.0f)) * model;
    
    auto vp = calculate_vp_matrix(target);
    cap_program.uniformMatrix4f("VP", vp);
    cap_program.uniformMatrix4f("model", model);
    cap_program.uniform1f("cap_alpha", (float)cap_alpha);
    
    static auto start_time = std::chrono::steady_clock::now();
    auto current_time = std::chrono::steady_clock::now();
    float elapsed = std::chrono::duration<float>(current_time - start_time).count();
    cap_program.uniform1f("time", elapsed);

    cap_program.uniform1f("u_num_sides", (float)num_sides);
    
    auto& buffer = is_top ? top_cap_buffer : bottom_cap_buffer;
    auto tex_id = wf::gles_texture_t::from_aux(buffer).tex_id;
    GL_CALL(glBindTexture(GL_TEXTURE_2D, tex_id));
    
    GL_CALL(glDisable(GL_CULL_FACE));
    GL_CALL(glDrawArrays(GL_TRIANGLE_FAN, 0, num_sides + 2));
    
    cap_program.deactivate();
}


    
    // Render cap textures (call this in schedule_instructions)
void render_cap_textures()
{
    if (!enable_caps)
        return;
        
    const float scale = output->handle->scale;
    auto bbox = output->get_layout_geometry();
    
    // Allocate cap buffers
    top_cap_buffer.allocate(wf::dimensions(bbox), scale);
    bottom_cap_buffer.allocate(wf::dimensions(bbox), scale);
    
    // Get the actual color values (cast option_wrapper to wf::color_t)
    wf::color_t top_color = cap_color_top;
    wf::color_t bottom_color = cap_color_bottom;
    
    // Render top cap
    wf::render_target_t top_target{top_cap_buffer};
    top_target.geometry = bbox;
    top_target.scale = scale;
    
    wf::gles::bind_render_buffer(top_target);
    // Use 1.0f for alpha - transparency is controlled by cap_alpha uniform
   GL_CALL(glClearColor(top_color.r, top_color.g, top_color.b, 1.0f));
    GL_CALL(glClear(GL_COLOR_BUFFER_BIT));
    
    // Render bottom cap
    wf::render_target_t bottom_target{bottom_cap_buffer};
    bottom_target.geometry = bbox;
    bottom_target.scale = scale;
    
    wf::gles::bind_render_buffer(bottom_target);
    // Use 1.0f for alpha here too
    GL_CALL(glClearColor(bottom_color.r, bottom_color.g, bottom_color.b, 1.0f));
    GL_CALL(glClear(GL_COLOR_BUFFER_BIT));
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

            GL_CALL(glClearColor(0.0f, 0.0f, 0.0f, 1.0f));
            GL_CALL(glEnable(GL_DEPTH_TEST));
            GL_CALL(glDepthFunc(GL_LESS));
            GL_CALL(glDepthMask(GL_TRUE));
            GL_CALL(glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT));

            // RENDER SHADER BACKGROUND FIRST (replaces background->render_frame)
            render_shader_background(data.target);

             GL_CALL(glClear(GL_DEPTH_BUFFER_BIT));

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

            program.attrib_pointer("position", 2, 0, vertexData);
            program.attrib_pointer("uvPosition", 2, 0, coordData);
            program.uniformMatrix4f("VP", vp);
            
            if (tessellation_support)
            {
                program.uniform1i("deform", use_deform);
                program.uniform1i("light", use_light);
                program.uniform1f("ease", animation.cube_animation.ease_deformation);
                
                GLint loc = glGetUniformLocation(program.get_program_id(wf::TEXTURE_TYPE_RGBA), "cameraYOffset");
                if (loc >= 0)
                {
                    GL_CALL(glUniform1f(loc, camera_y_offset));
                }
            }

            GL_CALL(glEnable(GL_CULL_FACE));
            GL_CALL(glEnable(GL_BLEND));
            GL_CALL(glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));
            
 
        // RENDER BOTTOM CAPS FIRST
        // render_cap handles its own state, so just call it
        for (int row = (int)buffers_rows.size() - 1; row >= 0; row--)
        {
            float vertical_offset = -(row + 1) * CUBE_VERTICAL_SPACING;
            float cap_y = vertical_offset - 0.5f;
            render_cap(false, cap_y, data.target);
        }
        render_cap(false, -0.5f, data.target);
        
        // RESTORE CUBE PROGRAM STATE after caps
        program.use(wf::TEXTURE_TYPE_RGBA);
        program.attrib_pointer("position", 2, 0, vertexData);
        program.attrib_pointer("uvPosition", 2, 0, coordData);
        program.uniformMatrix4f("VP", vp);
        GL_CALL(glEnable(GL_CULL_FACE));
        GL_CALL(glDepthMask(GL_TRUE));  // Restore depth writing for cubes
        
        // RENDER CUBE BACK FACES
        for (int row = (int)buffers_rows.size() - 1; row >= 0; row--)
        {
            float vertical_offset = -(row + 1) * CUBE_VERTICAL_SPACING;
            render_cube(GL_CCW, buffers_rows[row], vertical_offset, 1.0f);
        }
        render_cube(GL_CCW, buffers, 0.0f, 1.0f);
        
        // RENDER CUBE FRONT FACES
        for (int row = (int)buffers_rows.size() - 1; row >= 0; row--)
        {
            float vertical_offset = -(row + 1) * CUBE_VERTICAL_SPACING;
            render_cube(GL_CW, buffers_rows[row], vertical_offset, 1.0f);
        }
        render_cube(GL_CW, buffers, 0.0f, 1.0f);
        
        // RENDER TOP CAPS LAST
        // Caps handle their own blending/depth state
        render_cap(true, 0.5f, data.target);
        for (int row = (int)buffers_rows.size() - 1; row >= 0; row--)
        {
            float vertical_offset = -(row + 1) * CUBE_VERTICAL_SPACING;
            float cap_y = vertical_offset + 0.5f;
            render_cap(true, cap_y, data.target);
        }
        
        // RESTORE STATE for window popout cubes
        program.use(wf::TEXTURE_TYPE_RGBA);
        program.attrib_pointer("position", 2, 0, vertexData);
        program.attrib_pointer("uvPosition", 2, 0, coordData);
        program.uniformMatrix4f("VP", vp);
        GL_CALL(glEnable(GL_CULL_FACE));
        GL_CALL(glDepthFunc(GL_LESS));
        GL_CALL(glDepthMask(GL_TRUE));
        
        if (enable_window_popout)
        {
            float scale = popout_scale_animation;
            
            for (int row = (int)buffers_windows_rows.size() - 1; row >= 0; row--)
            {
                float vertical_offset = -(row + 1) * CUBE_VERTICAL_SPACING;
                render_cube(GL_CCW, buffers_windows_rows[row], vertical_offset, scale);
            }
            render_cube(GL_CCW, buffers_windows, 0.0f, scale);
            
            for (int row = (int)buffers_windows_rows.size() - 1; row >= 0; row--)
            {
                float vertical_offset = -(row + 1) * CUBE_VERTICAL_SPACING;
                render_cube(GL_CW, buffers_windows_rows[row], vertical_offset, scale);
            }
            render_cube(GL_CW, buffers_windows, 0.0f, scale);
        }
            
            GL_CALL(glDisable(GL_BLEND));
            GL_CALL(glDisable(GL_CULL_FACE));
            GL_CALL(glDisable(GL_DEPTH_TEST));
            program.deactivate();
        });
    }


wf::effect_hook_t pre_hook = [=] ()
{
    update_view_matrix();
    wf::scene::damage_node(render_node, render_node->get_bounding_box());
    
    if (animation.cube_animation.running() || camera_y_offset.running() || popout_scale_animation.running())
    {
        output->render->schedule_redraw();
    } else if (animation.in_exit)
    {
        deactivate();
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


// Fixed pointer_scrolled with proper zoom focal point adjustment
void pointer_scrolled(double amount)
{
    if (animation.in_exit)
    {
        return;
    }


    animation.cube_animation.offset_y.restart_with_end(
        animation.cube_animation.offset_y.end);
    animation.cube_animation.rotation.restart_with_end(
        animation.cube_animation.rotation.end);
    animation.cube_animation.ease_deformation.restart_with_end(
        animation.cube_animation.ease_deformation.end);

    float target_zoom = animation.cube_animation.zoom;
    float start_zoom  = target_zoom;

    target_zoom +=
        std::min(std::pow(target_zoom, 1.5f), ZOOM_MAX) * amount * ZVelocity;
    target_zoom = std::min(std::max(target_zoom, ZOOM_MIN), ZOOM_MAX);
    animation.cube_animation.zoom.set(start_zoom, target_zoom);

    // Adjust offset_z based on camera_y_offset to change zoom focal point
    float base_offset = identity_z_offset + Z_OFFSET_NEAR;
    float y_distance = std::abs(camera_y_offset);
    float z_adjust = std::sqrt(base_offset * base_offset + y_distance * y_distance) - base_offset;
    
    animation.cube_animation.offset_z.restart_with_end(base_offset + z_adjust);

    animation.cube_animation.start();
    output->render->schedule_redraw();
}


    void fini() override
    {
        if (output->is_plugin_active(grab_interface.name))
        {
            deactivate();
        }

        wf::gles::run_in_context_if_gles([&]
        {
            program.free_resources();
            cap_program.free_resources();
            background_program.free_resources();
            
            if (background_vbo)
            {
                GL_CALL(glDeleteBuffers(1, &background_vbo));
            }
            
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