#include "obj-renderer.hpp"
#include "glb-loader.hpp"
#include "glb-animation.hpp"
#include <algorithm>
#include <cctype>
#include <wayfire/util/log.hpp>
#include <wayfire/img.hpp>
#include <cstdlib>
#include <cstddef>
#include <filesystem>
#include <map>
#include <stdexcept>

void cube_obj_renderer_t::clear_mesh()
{
    count = 0;
    animation.reset(); animated_vertices.clear(); restart_animation(); last_clip = -2;
    if (buffer) glDeleteBuffers(1, &buffer);
    buffer = 0;
    if (!textures.empty()) glDeleteTextures(textures.size(), textures.data());
    textures.clear(); material_textures.clear(); materials.clear(); batches.clear();
}

void cube_obj_renderer_t::load(const std::string& path)
{
    if (path == loaded_path) return;
    loaded_path = path;
    clear_mesh();
    if (path.empty()) return;
    std::string expanded = path;
    if (path.rfind("~/", 0) == 0)
        if (auto home = std::getenv("HOME")) expanded = std::string(home) + path.substr(1);
    cube_obj_mesh_t mesh;
    std::string error;
    auto extension = std::filesystem::path(expanded).extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return std::tolower(c); });
    const bool loaded = extension == ".glb" ? load_cube_glb(expanded, mesh, error) : load_cube_obj(expanded, mesh, error);
    if (!loaded) { LOGE("Cube model: ", error); return; }
    uv_top_left = mesh.uv_top_left;
    animation = mesh.animation;
    if (animation)
    {
        auto clips = animation->clips();
        for (size_t i = 0; i < clips.size(); ++i) LOGI("Cube animation ", i, ": ", clips[i]);
    }
    for (auto& warning : mesh.warnings) LOGW("Cube OBJ: ", warning);
    glGenBuffers(1, &buffer);
    GLint old; glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &old);
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    glBufferData(GL_ARRAY_BUFFER, mesh.vertices.size() * sizeof(mesh.vertices[0]), mesh.vertices.data(), animation ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, old);
    count = mesh.vertices.size();
    materials = std::move(mesh.materials);
    batches = std::move(mesh.batches);
    GLint old_active, old_texture;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &old_active);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &old_texture);
    GLuint white;
    glGenTextures(1, &white); glBindTexture(GL_TEXTURE_2D, white);
    const unsigned char pixel[] = {255, 255, 255, 255};
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    textures.push_back(white);
    std::map<std::string, GLuint> cache;
    auto texture = [&](const std::string& filename) -> GLuint
    {
        if (filename.empty()) return white;
        if (cache.count(filename)) return cache[filename];
        // Keep excessive material libraries from exhausting compositor GPU memory.
        if (cache.size() >= 128) { LOGW("Cube OBJ: texture limit reached"); return white; }
        GLuint id = 0;
        std::error_code ec;
        if (mesh.images.count(filename))
        {
            const auto& image = mesh.images.at(filename);
            GLint max_size; glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_size);
            if (image.width <= max_size && image.height <= max_size)
            {
                glGenTextures(1, &id); glBindTexture(GL_TEXTURE_2D, id);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, image.width, image.height, 0,
                    GL_RGBA, GL_UNSIGNED_BYTE, image.rgba.data());
            }
        } else if (std::filesystem::is_regular_file(filename, ec))
        {
            glGenTextures(1, &id); glBindTexture(GL_TEXTURE_2D, id);
            if (!image_io::load_from_file(filename, GL_TEXTURE_2D))
            { glDeleteTextures(1, &id); id = 0; }
        }
        if (!id)
        {
            LOGW("Cube OBJ: cannot load texture: ", filename);
            return cache[filename] = white;
        }
        // Repeat is implemented in the shader for GLES2 NPOT compatibility.
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        textures.push_back(id);
        return cache[filename] = id;
    };
    for (const auto& m : materials)
        material_textures.push_back({texture(m.diffuse_map.path), texture(m.specular_map.path),
            texture(m.opacity_map.path)});
    glBindTexture(GL_TEXTURE_2D, old_texture);
    glActiveTexture(old_active);
}

void cube_obj_renderer_t::animate(double frame_time, bool playing, int clip, double speed)
{
    if (!animation) return;
    auto clips = animation->clips();
    clip = std::clamp(clip, 0, int(clips.size()) - 1);
    if (animation_start < 0 || clip != last_clip || playing != last_playing)
    {
        animation_start = frame_time;
        last_animation_time = -1;
    }
    const double time = playing ? std::max(0.0, frame_time - animation_start) * speed : 0;
    if (time == last_animation_time && clip == last_clip && playing == last_playing) return;
    try
    {
        animation->sample(time, playing ? clip : -1, animated_vertices);
        if (animated_vertices.size() != size_t(count)) throw std::runtime_error("Animation changed mesh topology");
        GLint previous; glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &previous);
        glBindBuffer(GL_ARRAY_BUFFER, buffer);
        glBufferSubData(GL_ARRAY_BUFFER, 0, animated_vertices.size() * sizeof(cube_obj_vertex_t), animated_vertices.data());
        glBindBuffer(GL_ARRAY_BUFFER, previous);
        last_animation_time = time; last_clip = clip; last_playing = playing;
    } catch (const std::exception& e)
    {
        LOGE("Cube GLB animation stopped: ", e.what());
        animation.reset();
    }
}

static const char *object_vertex_shader = R"(
#version 100
attribute vec3 position;
attribute vec3 normal;
attribute vec2 uv;
uniform mat4 VP, model;
varying mediump vec3 n;
varying highp vec2 texcoord;
void main() {
    n = mat3(model) * normal;
    texcoord = uv;
    gl_Position = VP * model * vec4(position, 1.0);
}
)";

static const char *object_fragment_shader = R"(
#version 100
precision mediump float;
varying mediump vec3 n;
varying highp vec2 texcoord;
uniform sampler2D diffuse_texture, specular_texture, opacity_texture;
uniform vec3 diffuse, ambient, specular, emission;
uniform float shininess, opacity;
uniform int illumination, alpha_mode;
uniform float alpha_cutoff;
uniform bool uv_top_left;
uniform vec4 diffuse_transform, specular_transform, opacity_transform;
uniform vec3 map_clamp;
vec2 map_uv(vec4 transform, float clamp_uv) {
    vec2 p = texcoord * transform.xy + transform.zw;
    p = clamp_uv > 0.5 ? clamp(p, 0.0, 1.0) : fract(p);
    // OBJ UV origin is bottom-left, image_io uploads top image row first.
    return vec2(p.x, uv_top_left ? p.y : 1.0 - p.y);
}
void main() {
    vec4 texel = texture2D(diffuse_texture, map_uv(diffuse_transform, map_clamp.x));
    float alpha = texel.a * opacity * texture2D(opacity_texture,
        map_uv(opacity_transform, map_clamp.z)).r;
    if (alpha_mode == 1) alpha = 1.0;
    if (alpha_mode == 2) { if (alpha < alpha_cutoff) discard; alpha = 1.0; }
    // Coverage transparency keeps shared cube depth correct without order-dependent
    // blending. Exact cutouts leave no color or depth; fractional alpha is dithered.
    vec2 p = mod(floor(gl_FragCoord.xy), 4.0);
    float low = mod(p.x, 2.0) * 2.0 + mod(p.y, 2.0) * 3.0;
    low = mod(low, 4.0);
    float high = mod(floor(p.x / 2.0) * 2.0 + floor(p.y / 2.0) * 3.0, 4.0);
    if (alpha <= (low * 4.0 + high + 0.5) / 16.0) discard;
    vec3 normal = normalize(n) * (gl_FrontFacing ? 1.0 : -1.0);
    vec3 light_dir = normalize(vec3(0.4, 0.8, 1.0));
    float lambert = max(dot(normal, light_dir), 0.0);
    vec3 base = diffuse * texel.rgb;
    vec3 color = base;
    if (illumination > 0) color = base * (0.18 + ambient * 0.25 + 0.72 * lambert);
    if (illumination > 1 && lambert > 0.0) {
        vec3 halfway = normalize(light_dir + vec3(0.0, 0.0, 1.0));
        color += specular * texture2D(specular_texture,
            map_uv(specular_transform, map_clamp.y)).rgb * pow(max(dot(normal, halfway), 0.0), shininess);
    }
    gl_FragColor = vec4(clamp(color + emission, 0.0, 1.0), 1.0);
}
)";

void cube_obj_renderer_t::render(const glm::mat4& vp, const glm::mat4& model)
{
    if (!count) return;
    if (!program.get_program_id(wf::TEXTURE_TYPE_RGBA))
        program.set_simple(OpenGL::compile_program(object_vertex_shader, object_fragment_shader));
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS); glDepthMask(GL_TRUE);
    glDisable(GL_BLEND); glDisable(GL_CULL_FACE);
    program.use(wf::TEXTURE_TYPE_RGBA);
    program.uniformMatrix4f("VP", vp); program.uniformMatrix4f("model", model);
    GLint old, old_active, bindings[3];
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &old);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &old_active);
    for (int i = 0; i < 3; ++i)
    { glActiveTexture(GL_TEXTURE0 + i); glGetIntegerv(GL_TEXTURE_BINDING_2D, &bindings[i]); }
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    program.attrib_pointer("position", 3, sizeof(cube_obj_vertex_t), (void*)offsetof(cube_obj_vertex_t, position));
    program.attrib_pointer("normal", 3, sizeof(cube_obj_vertex_t), (void*)offsetof(cube_obj_vertex_t, normal));
    program.attrib_pointer("uv", 2, sizeof(cube_obj_vertex_t), (void*)offsetof(cube_obj_vertex_t, uv));
    program.uniform1i("uv_top_left", uv_top_left);
    program.uniform1i("diffuse_texture", 0); program.uniform1i("specular_texture", 1);
    program.uniform1i("opacity_texture", 2);
    auto rgb = [&](const char *name, glm::vec3 c) { program.uniform3f(name, c.r, c.g, c.b); };
    auto transform = [&](const char *name, const cube_obj_map_t& map) {
        program.uniform4f(name, glm::vec4(map.scale, map.offset));
    };
    for (const auto& batch : batches)
    {
        if (!batch.count) continue;
        const auto& m = materials[batch.material];
        const auto& t = material_textures[batch.material];
        rgb("diffuse", m.diffuse); rgb("ambient", m.ambient);
        rgb("specular", m.specular); rgb("emission", m.emission);
        program.uniform1f("shininess", m.shininess); program.uniform1f("opacity", m.opacity);
        program.uniform1i("illumination", m.illumination);
        program.uniform1i("alpha_mode", m.alpha_mode);
        program.uniform1f("alpha_cutoff", m.alpha_cutoff);
        transform("diffuse_transform", m.diffuse_map); transform("specular_transform", m.specular_map);
        transform("opacity_transform", m.opacity_map);
        program.uniform3f("map_clamp", m.diffuse_map.clamp, m.specular_map.clamp, m.opacity_map.clamp);
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, t.diffuse);
        glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, t.specular);
        glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, t.opacity);
        glDrawArrays(GL_TRIANGLES, batch.first, batch.count);
    }
    program.deactivate();
    glBindBuffer(GL_ARRAY_BUFFER, old);
    for (int i = 0; i < 3; ++i)
    { glActiveTexture(GL_TEXTURE0 + i); glBindTexture(GL_TEXTURE_2D, bindings[i]); }
    glActiveTexture(old_active);
}

void cube_obj_renderer_t::release()
{
    clear_mesh(); loaded_path.clear(); program.free_resources();
}
