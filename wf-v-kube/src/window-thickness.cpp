#include "window-thickness.hpp"
#include "thickness-visibility.hpp"
#ifdef VERTICAL_KUBE_USE_GLES32
#include <GLES3/gl32.h>
#include <vector>

static const char *thickness_vertex = R"(#version 320 es
precision highp float;
precision highp int;
uniform ivec2 dimensions;
uniform ivec2 scan_origin, scan_size;
flat out ivec2 pixel;
void main() {
    pixel = scan_origin + ivec2(gl_VertexID % scan_size.x, gl_VertexID / scan_size.x);
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
uniform bool desktop;
float alpha(ivec2 p) {
    if (any(lessThan(p,ivec2(0))) || any(greaterThanEqual(p,dimensions)))
        return desktop ? 0.0 : 1.0;
    float a=texelFetch(pixels,p,0).a;
    if (desktop) {
        // Match the desktop shader's rounded corners and feathered perimeter.
        vec2 uv=(vec2(p)+.5)/vec2(dimensions);
        vec2 q=abs(uv-.5)-.42;
        float corner=(q.x>0.0 && q.y>0.0) ?
            1.0-smoothstep(.07,.09,length(q)) : 1.0;
        float edge=smoothstep(0.0,.01,min(min(uv.x,1.0-uv.x),min(uv.y,1.0-uv.y)));
        a *= min(corner,edge);
    }
    return a;
}
vec3 front(vec2 p) {
    vec3 world=(model*vec4(p/vec2(dimensions)-.5,0,1)).xyz;
    if (deform>0) {
        float d=max(length(world.xz),.000001);
        world.xz *= pow(deform==1 ? .5*scale/d : d/(.5*scale),ease);
    }
    return world;
}
void point(vec2 p, float depth) {
    vec3 world=front(p);
    // Extrude along the deformed surface normal. Deforming the back plane
    // separately would collapse cylinder thickness onto its fixed radius.
    vec3 dx=front(p+vec2(.5,0))-front(p-vec2(.5,0));
    vec3 dy=front(p+vec2(0,.5))-front(p-vec2(0,.5));
    world -= normalize(cross(dx,dy))*depth*scale;
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
    if (alpha(p)<cutoff) return;
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
    int deform, float ease, float scale, bool desktop)
{
#ifdef VERTICAL_KUBE_USE_GLES32
    if (thickness<=.00001f || opacity<=0) return;
    if (!thickness_visible(vp,model,{-.5f,-.5f},{.5f,.5f},deform,ease,scale,thickness)) return;
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
    program.uniform1i("desktop",desktop);
    program.uniformMatrix4f("VP",vp); program.uniformMatrix4f("model",model);
    program.uniform1f("thickness",thickness); program.uniform1f("cutoff",cutoff);
    program.uniform1f("opacity",opacity); program.uniform1f("ease",ease);
    program.uniform1f("scale",scale); program.uniform1i("deform",deform);
    // Cull tiles before submitting points: clipping in the fragment shader
    // would still run the expensive silhouette geometry stage for every pixel.
    constexpr int tile=128;
    GLuint id=program.get_program_id(wf::TEXTURE_TYPE_RGBA);
    GLint origin_location=glGetUniformLocation(id,"scan_origin");
    GLint size_location=glGetUniformLocation(id,"scan_size");
    struct scan_t { int x,y,width,height; };
    std::vector<scan_t> scans;
    for (int y=0; y<size.height; y+=tile)
    {
        int height=std::min(tile,size.height-y), start=-1;
        auto submit=[&](int end)
        {
            if (start<0) return;
            if (!scans.empty() && scans.back().x==start && scans.back().width==end-start &&
                scans.back().y+scans.back().height==y)
                scans.back().height+=height;
            else scans.push_back({start,y,end-start,height});
            start=-1;
        };
        for (int x=0; x<size.width; x+=tile)
        {
            int end=std::min(x+tile,size.width);
            // One-pixel padding includes finite-difference normals at the rim.
            glm::vec2 low(float(x-1)/size.width-.5f,float(y-1)/size.height-.5f);
            glm::vec2 high(float(end+1)/size.width-.5f,float(y+height+1)/size.height-.5f);
            if (thickness_visible(vp,model,low,high,deform,ease,scale,thickness))
            { if (start<0) start=x; }
            else submit(x);
        }
        submit(size.width);
    }
    for (const auto& scan : scans)
    {
        glUniform2i(origin_location,scan.x,scan.y);
        glUniform2i(size_location,scan.width,scan.height);
        glDrawArrays(GL_POINTS,0,scan.width*scan.height);
    }
    program.deactivate();
#endif
}
