#include "shader-background.hpp"
#include <chrono>
#include <algorithm>
#include <cmath>

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
uniform vec2 u_uv_origin;
uniform vec2 u_uv_scale;
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

float field(in vec3 p, float strength, vec3 field_offset) {
    float accum = 0.;
    float prev = 0.;
    float tw = 0.;

    for (int i = 0; i < 6; ++i) {
        float mag = dot(p, p);
        p = abs(p) / mag + field_offset;
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

    vec2 fragCoord = (u_uv_origin + v_uv * u_uv_scale) * iResolution;
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

    float field_strength = 7.0 + 0.03 * log(1.e-6 + fract(sin(u_time) * 4373.11));
    vec3 field_offset = vec3(-0.5, -0.8 + 0.1 * sin(u_time * 0.2 + 2.0),
        -1.1 + 0.3 * cos(u_time * 0.15));
    vec3 backCol2 = vec3(0.0);
    for (int r = 0; r < volsteps; r++) {
        vec3 p2 = from + (s + zoffset) * dir;
        vec3 p3 = (from + (s3 + zoffset) * dir) * (1.9 / zoom);

        p2 = abs(vec3(tile) - mod(p2, vec3(tile * 2.0)));
        p3 = abs(vec3(tile) - mod(p3, vec3(tile * 2.0)));

        t3 = field(p3, field_strength, field_offset);

        float pa, a = pa = 0.0;
        for (int i = 0; i < iterations; i++) {
            p2 = abs(p2) / dot(p2, p2) - formuparam;
            // With four iterations only the last two lengths contribute.
            if (i == 2) pa = length(p2);
            if (i == 3) a = abs(length(p2) - pa);
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

static const char *background_copy_shader = R"(
#version 100
precision mediump float;
varying vec2 v_uv;
uniform sampler2D source;
void main() { gl_FragColor = texture2D(source, v_uv); }
)";

wf_cube_shader_background::wf_cube_shader_background()
{
    wf::gles::run_in_context([&]
    {
        copy_program.set_simple(OpenGL::compile_program(background_vertex_shader, background_copy_shader));
        background_program.set_simple(OpenGL::compile_program(
            background_vertex_shader, background_fragment_shader));
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
    });
}

wf_cube_shader_background::~wf_cube_shader_background()
{
    wf::gles::run_in_context_if_gles([&]
    {
        background_program.free_resources();
        copy_program.free_resources();
        scaled_buffer.free();
        GL_CALL(glDeleteBuffers(1, &background_vbo));
    });
}

void wf_cube_shader_background::render_frame(const wf::render_target_t& target,
    wf_cube_animation_attribs& attribs)
{
    if (background_program.get_program_id(wf::TEXTURE_TYPE_RGBA) == 0)
    {
        OpenGL::clear(fallback_color);
        return;
    }

    GL_CALL(glDisable(GL_DEPTH_TEST));
    GL_CALL(glDisable(GL_BLEND));
    GL_CALL(glDisable(GL_SCISSOR_TEST));
    const float scale = std::clamp(float(resolution_scale), 0.25f, 1.0f);
    const auto size = target.get_size();
    const bool reduced = scale < 0.999f;
    if (reduced)
    {
        scaled_buffer.allocate({std::max(1, int(std::ceil(size.width * scale))),
            std::max(1, int(std::ceil(size.height * scale)))});
        wf::gles::bind_render_buffer(scaled_buffer.get_renderbuffer());
    } else
    {
        scaled_buffer.free();
    }

    background_program.use(wf::TEXTURE_TYPE_RGBA);

    background_program.uniform1f("u_time", attribs.frame_time);
    background_program.uniform2f("u_resolution", attribs.background_size.x, attribs.background_size.y);
    background_program.uniform2f("u_uv_origin", attribs.background_uv_origin.x, attribs.background_uv_origin.y);
    background_program.uniform2f("u_uv_scale", attribs.background_uv_scale.x, attribs.background_uv_scale.y);

    GL_CALL(glBindBuffer(GL_ARRAY_BUFFER, background_vbo));
    background_program.attrib_pointer("position", 2, 0, nullptr);

    GL_CALL(glDrawArrays(GL_TRIANGLE_STRIP, 0, 4));

    background_program.deactivate();
    if (reduced)
    {
        wf::gles::bind_render_buffer(target);
        copy_program.use(wf::TEXTURE_TYPE_RGBA);
        copy_program.uniform1i("source", 0);
        GL_CALL(glActiveTexture(GL_TEXTURE0));
        GL_CALL(glBindTexture(GL_TEXTURE_2D, wf::gles_texture_t::from_aux(scaled_buffer).tex_id));
        GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
        GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
        GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
        GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
        copy_program.attrib_pointer("position", 2, 0, nullptr);
        GL_CALL(glDrawArrays(GL_TRIANGLE_STRIP, 0, 4));
        copy_program.deactivate();
        GL_CALL(glBindTexture(GL_TEXTURE_2D, 0));
    }
    GL_CALL(glBindBuffer(GL_ARRAY_BUFFER, 0));
}
