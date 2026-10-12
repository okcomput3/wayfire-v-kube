#include "window-thickness.hpp"
#ifdef VERTICAL_KUBE_USE_GLES32
#include <GLES3/gl32.h>
#include <stdexcept>

static const char *thickness_vertex = R"(#version 320 es
precision highp float;
precision highp int;
uniform ivec2 dimensions;
flat out ivec2 pixel;
void main() {
    pixel = ivec2(gl_VertexID % dimensions.x, gl_VertexID / dimensions.x);
    gl_Position = vec4(0,0,0,1);
})";
static const char *thickness_geometry = R"(#version 320 es
precision highp float;
precision highp int;
layout(points) in;
layout(triangle_strip, max_vertices=16) out;
flat in ivec2 pixel[];
flat out vec4 edge_color;
uniform sampler2D pixels;
uniform ivec2 dimensions;
uniform mat4 VP, model;
uniform float thickness, cutoff, opacity, ease, scale;
uniform int deform;
float alpha(ivec2 p) {
    if (any(lessThan(p,ivec2(0))) || any(greaterThanEqual(p,dimensions))) return 1.0;
    return texelFetch(pixels,p,0).a;
}
void point(vec2 p, float depth) {
    vec3 world=(model*vec4(p/vec2(dimensions)-.5,-depth,1)).xyz;
    if (deform>0) {
        float d=max(length(world.xz),.000001);
        world.xz *= pow(deform==1 ? .5*scale/d : d/(.5*scale),ease);
    }
    gl_Position=VP*vec4(world,1);
    EmitVertex();
}
void wall(vec2 a, vec2 b) {
    point(a,0.0); point(b,0.0); point(a,thickness); point(b,thickness);
    EndPrimitive();
}
void main() {
    ivec2 p=pixel[0];
    vec4 color=texelFetch(pixels,p,0);
    if (color.a<cutoff) return;
    edge_color=vec4(color.rgb/max(color.a,.0001),opacity);
    vec2 a=vec2(p), b=a+1.0;
    if (alpha(p+ivec2(-1,0))<cutoff) wall(a,vec2(a.x,b.y));
    if (alpha(p+ivec2(1,0))<cutoff) wall(vec2(b.x,a.y),b);
    if (alpha(p+ivec2(0,-1))<cutoff) wall(a,vec2(b.x,a.y));
    if (alpha(p+ivec2(0,1))<cutoff) wall(vec2(a.x,b.y),b);
})";
static const char *thickness_fragment = R"(#version 320 es
precision highp float;
flat in vec4 edge_color;
out vec4 color;
void main() {
    // Coverage preserves real depth without creating translucent sorting holes.
    float threshold=fract(dot(floor(gl_FragCoord.xy),vec2(.754877666,.569840296)));
    if (edge_color.a<=threshold) discard;
    color=vec4(edge_color.rgb,1);
})";
#endif

void window_thickness_t::render(wf::auxilliary_buffer_t& pixels, const glm::mat4& vp,
    const glm::mat4& model, float thickness, float cutoff, float opacity,
    int deform, float ease, float scale)
{
#ifdef VERTICAL_KUBE_USE_GLES32
    if (thickness<=.00001f || opacity<=0) return;
    if (!program.get_program_id(wf::TEXTURE_TYPE_RGBA))
    {
        GLuint id=glCreateProgram();
        GLuint shaders[]={OpenGL::compile_shader(thickness_vertex,GL_VERTEX_SHADER),
            OpenGL::compile_shader(thickness_geometry,GL_GEOMETRY_SHADER),
            OpenGL::compile_shader(thickness_fragment,GL_FRAGMENT_SHADER)};
        for (auto shader:shaders) glAttachShader(id,shader);
        glLinkProgram(id);
        for (auto shader:shaders) glDeleteShader(shader);
        GLint linked=0; glGetProgramiv(id,GL_LINK_STATUS,&linked);
        if (!linked) { glDeleteProgram(id); return; }
        program.set_simple(id);
    }
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL); glDepthMask(GL_TRUE);
    glDisable(GL_BLEND); glDisable(GL_CULL_FACE);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D,wf::gles_texture_t::from_aux(pixels).tex_id);
    program.use(wf::TEXTURE_TYPE_RGBA);
    auto size=pixels.get_size();
    glUniform2i(glGetUniformLocation(program.get_program_id(wf::TEXTURE_TYPE_RGBA),"dimensions"),size.width,size.height);
    program.uniform1i("pixels",0);
    program.uniformMatrix4f("VP",vp); program.uniformMatrix4f("model",model);
    program.uniform1f("thickness",thickness); program.uniform1f("cutoff",cutoff);
    program.uniform1f("opacity",opacity); program.uniform1f("ease",ease);
    program.uniform1f("scale",scale); program.uniform1i("deform",deform);
    glDrawArrays(GL_POINTS,0,size.width*size.height);
    program.deactivate();
#endif
}
