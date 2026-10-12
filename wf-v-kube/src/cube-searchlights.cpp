#include "cube-searchlights.hpp"
#include "searchlight-path.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstddef>

static const char *searchlight_vertex = R"(
#version 100
attribute vec3 position;
attribute vec2 beam_uv;
attribute vec3 beam_color;
uniform mat4 VP;
varying mediump vec2 uv;
varying mediump vec3 color;
void main() { uv = beam_uv; color = beam_color; gl_Position = VP * vec4(position, 1.0); }
)";
static const char *searchlight_fragment = R"(
#version 100
precision mediump float;
varying mediump vec2 uv;
varying mediump vec3 color;
uniform float intensity;
void main() {
    float edge = pow(max(0.0, 1.0 - abs(uv.y)), 2.5);
    float fade = pow(max(0.0, 1.0 - uv.x), 1.6);
    float source = smoothstep(0.0, 0.025, uv.x);
    float glow = edge * fade * source * intensity;
    gl_FragColor = vec4(color * glow, 0.0);
}
)";

void cube_searchlights_t::render(const glm::mat4& vp, const glm::vec3& view_direction, int rows, float row_spacing,
    float rotation, float time, float activation, float speed, float intensity,
    float length, int count, bool refract, float refractive_index, int faces, int deformation)
{
    if (activation <= .0001f || intensity <= 0) return;
    count = std::clamp(count, 1, 12);
    if (!program.get_program_id(wf::TEXTURE_TYPE_RGBA))
        program.set_simple(OpenGL::compile_program(searchlight_vertex, searchlight_fragment));
    struct vertex_t { glm::vec3 position; glm::vec2 uv; glm::vec3 color; };
    std::vector<vertex_t> vertices;
    vertices.reserve(rows * count * 6);
    // A shared view direction keeps beam geometry identical across panoramic slices.
    const float phase = time * speed;
    constexpr float pi = 3.14159265358979323846f;
    const auto boundary = refract ? searchlight_boundary(faces, deformation, activation, rotation, rows, row_spacing) :
        std::vector<searchlight_boundary_t>{};
    for (int row = 0; row < rows; ++row)
        for (int beam = 0; beam < count; ++beam)
        {
            float angle = rotation + phase * (row % 2 ? -.8f : 1.f) + 2*pi*beam/count + row*.73f;
            float tilt = .55f * std::sin(phase*.71f + beam*1.91f + row*.83f);
            glm::vec3 direction(std::cos(angle)*std::cos(tilt), std::sin(tilt), std::sin(angle)*std::cos(tilt));
            glm::vec3 origin(0, row*row_spacing, 0);
            float hue=phase*.22f+beam*2*pi/count+row*1.2f;
            glm::vec3 color(.55f+.45f*std::cos(hue),.55f+.45f*std::cos(hue-2*pi/3),.55f+.45f*std::cos(hue+2*pi/3));
            float reach = length * activation;
            // Keep width and longitudinal fading continuous across optical boundaries.
            for (const auto& segment : searchlight_trace(origin, direction, reach,
                std::max(1.f, refractive_index), boundary))
            {
                auto ray = glm::normalize(segment.b-segment.a);
                auto side = glm::cross(ray, view_direction);
                if (glm::length(side)<.01f) side=glm::cross(ray, glm::vec3(0,1,0));
                if (glm::length(side)<.01f) side=glm::cross(ray, glm::vec3(1,0,0));
                side=glm::normalize(side);
                float start = segment.start / reach, end = segment.end / reach;
                auto w0 = side * (.006f*activation + segment.start*.065f);
                auto w1 = side * (.006f*activation + segment.end*.065f);
                vertex_t a{segment.a-w0,{start,-1},color}, b{segment.a+w0,{start,1},color};
                vertex_t c{segment.b+w1,{end,1},color}, d{segment.b-w1,{end,-1},color};
                vertices.insert(vertices.end(),{a,b,c,a,c,d});
            }
        }
    GLint old_buffer, src_rgb, dst_rgb, src_alpha, dst_alpha, depth_func;
    GLboolean depth_mask;
    bool blend=glIsEnabled(GL_BLEND),depth=glIsEnabled(GL_DEPTH_TEST),cull=glIsEnabled(GL_CULL_FACE);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&old_buffer);
    glGetIntegerv(GL_BLEND_SRC_RGB,&src_rgb);glGetIntegerv(GL_BLEND_DST_RGB,&dst_rgb);
    glGetIntegerv(GL_BLEND_SRC_ALPHA,&src_alpha);glGetIntegerv(GL_BLEND_DST_ALPHA,&dst_alpha);
    glGetIntegerv(GL_DEPTH_FUNC,&depth_func);glGetBooleanv(GL_DEPTH_WRITEMASK,&depth_mask);
    if(!vbo) glGenBuffers(1,&vbo);
    glBindBuffer(GL_ARRAY_BUFFER,vbo);
    glBufferData(GL_ARRAY_BUFFER,vertices.size()*sizeof(vertex_t),vertices.data(),GL_STREAM_DRAW);
    glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LEQUAL);glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);glEnable(GL_BLEND);glBlendFunc(GL_ONE,GL_ONE);
    program.use(wf::TEXTURE_TYPE_RGBA);
    program.uniformMatrix4f("VP",vp);
    program.uniform1f("intensity",intensity*activation*.7f);
    program.attrib_pointer("position",3,sizeof(vertex_t),(void*)offsetof(vertex_t,position));
    program.attrib_pointer("beam_uv",2,sizeof(vertex_t),(void*)offsetof(vertex_t,uv));
    program.attrib_pointer("beam_color",3,sizeof(vertex_t),(void*)offsetof(vertex_t,color));
    glDrawArrays(GL_TRIANGLES,0,vertices.size());
    program.deactivate();
    glBindBuffer(GL_ARRAY_BUFFER,old_buffer);
    glDepthMask(depth_mask);glDepthFunc(depth_func);
    glBlendFuncSeparate(src_rgb,dst_rgb,src_alpha,dst_alpha);
    if(!blend) glDisable(GL_BLEND);
    if(!depth) glDisable(GL_DEPTH_TEST);
    if(cull) glEnable(GL_CULL_FACE);
}

void cube_searchlights_t::release()
{
    if(vbo) glDeleteBuffers(1,&vbo);
    vbo=0;program.free_resources();
}
