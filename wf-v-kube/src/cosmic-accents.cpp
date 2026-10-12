#include "cosmic-accents.hpp"
#include <glm/gtc/matrix_inverse.hpp>
#include <vector>
#include <cmath>
#include <algorithm>
#include <cstddef>

static const char *cosmic_vertex = R"(#version 100
attribute vec3 position;
attribute vec2 texcoord;
attribute vec3 tint;
attribute float kind;
uniform mat4 VP;
varying mediump vec2 uv;
varying mediump vec3 color;
varying mediump float sprite;
void main() {
    uv=texcoord; color=tint; sprite=kind;
    gl_Position=VP*vec4(position,1);
})";
static const char *cosmic_fragment = R"(#version 100
#extension GL_OES_standard_derivatives : enable
precision mediump float;
varying mediump vec2 uv;
varying mediump vec3 color;
varying mediump float sprite;
uniform float intensity;
void main() {
    float glow;
    if (sprite>.5) {
        float r=length(uv);
        float halo=exp(-5.0*r*r)*.18;
        float crosslight=exp(-abs(uv.x)*55.0)*pow(max(0.0,1.0-abs(uv.y)),3.0)
            +exp(-abs(uv.y)*55.0)*pow(max(0.0,1.0-abs(uv.x)),3.0);
        glow=(halo+crosslight)* (1.0-smoothstep(.75,1.0,r));
    } else {
        float d=abs(uv.y);
        float core=max(.10,min(.5,fwidth(uv.y)*.65));
        glow=exp(-d*d*8.0)*.3 + exp(-d*d/(core*core));
        glow*=1.0-smoothstep(.8,1.0,d);
    }
    gl_FragColor=vec4(color*glow*intensity,0);
})";

namespace {
constexpr float pi=3.14159265358979323846f;
// The same rounded polygon as capEdgeCoverage, before radial deformation.
float rim_radius(float angle, int faces) {
    float half=pi/faces, apothem=.5f/std::tan(half);
    float folded=std::remainder(angle,2*half);
    float rounding=std::min(.08f,apothem*.2f), inset=apothem-rounding;
    float low=0, high=apothem/std::cos(half);
    for (int i=0;i<14;++i) {
        float r=(low+high)*.5f;
        float x=std::max(r*std::abs(std::sin(folded))-inset*std::tan(half),0.f);
        float y=r*std::cos(folded)-inset;
        float distance=std::hypot(x,y)*(y<0?-1.f:1.f)-rounding;
        if (distance>-.004f) high=r; else low=r;
    }
    return (low+high)*.5f;
}
glm::vec3 hue(float phase) {
    // Alternate cool cyan and warm gold, like the reference's orbital trails.
    float t=.5f+.5f*std::sin(phase);
    return glm::mix(glm::vec3(.08f,.6f,1.f),glm::vec3(1.f,.48f,.08f),t);
}
}
void cosmic_accents_t::render(const glm::mat4& vp, const glm::mat4& view,
    int rows, float spacing, int faces, int deform, float rotation, float time,
    float ease, bool rims, bool rings, bool sparkles, float intensity, float speed, float ring_gap, float ring_width)
{
    if ((!rims && !rings && !sparkles) || ease<=.0001f || intensity<=0 || faces<3) return;
    if (!program.get_program_id(wf::TEXTURE_TYPE_RGBA))
        program.set_simple(OpenGL::compile_program(cosmic_vertex,cosmic_fragment));
    struct vertex_t { glm::vec3 position; glm::vec2 uv; glm::vec3 tint; float kind; };
    std::vector<vertex_t> vertices;
    vertices.reserve(rows*4096);
    auto inverse=glm::inverse(view);
    auto right=glm::normalize(glm::vec3(inverse*glm::vec4(1,0,0,0)));
    auto up=glm::normalize(glm::vec3(inverse*glm::vec4(0,1,0,0)));
    auto toward=glm::normalize(glm::vec3(inverse*glm::vec4(0,0,1,0)));
    float clock=time*speed;
    auto quad=[&](glm::vec3 a,glm::vec3 b,glm::vec3 c,glm::vec3 d,glm::vec3 tint,float kind) {
        vertex_t v0{a,{-1,-1},tint,kind},v1{b,{1,-1},tint,kind};
        vertex_t v2{c,{1,1},tint,kind},v3{d,{-1,1},tint,kind};
        vertices.insert(vertices.end(),{v0,v1,v2,v0,v2,v3});
    };
    auto line=[&](glm::vec3 a,glm::vec3 b,float width,glm::vec3 tint) {
        auto side=glm::cross(b-a,toward);
        if(glm::length(side)<1e-6f) return;
        side=glm::normalize(side)*width*ease;
        quad(a-side,b-side,b+side,a+side,tint,0);
    };
    float radius=std::hypot(.5f,.5f/std::tan(pi/faces));
    if(deform) radius*=std::pow(deform==1?.5f/radius:radius/.5f,ease);
    float orbit=radius+std::clamp(ring_gap,.02f,.6f);
    constexpr int steps=192;
    // All output slices share the same clock and billboard orientation.
    for(int row=0;row<rows;++row) {
        float y=row*spacing;
        if(rims) for(float cap : {-.5f,.5f}) {
            auto position=[&](float angle) {
                float r=rim_radius(angle,faces);
                if(deform) r*=std::pow(deform==1?.5f/r:r/.5f,ease);
                return glm::vec3(r*std::sin(angle+rotation),y+cap+(cap>0?.001f:-.001f),r*std::cos(angle+rotation));
            };
            for(int i=0;i<steps;++i) {
                float a=i*2*pi/steps,b=(i+1)*2*pi/steps;
                float pulse=.75f+.25f*std::pow(.5f+.5f*std::cos(a*3-clock*1.8f),8.f);
                line(position(a),position(b),.032f,hue(a*2+row-clock*.4f)*pulse);
            }
        }
        auto orbit_point=[&](float a,int ring) {
            float radius=orbit+ring*.035f;
            return glm::vec3(radius*std::sin(a),y+(ring?-.40f:.40f)+.10f*std::sin(a+ring*1.7f+clock*.12f),radius*std::cos(a));
        };
        // Continuous moving thickness profiles: a hairline trail widens into
        // luminous knots. Shared endpoint widths/tangents avoid segment seams.
        auto ring_profile=[&](float a,int ring) {
            float phase=a-clock*(ring?-.45f:.6f)+row;
            float head=std::pow(.5f+.5f*std::cos(phase),18.f);
            float knots=std::pow(.5f+.5f*std::cos(phase*3+ring),24.f);
            return .16f+.65f*head+.32f*knots;
        };
        if(rings) for(int ring=0;ring<2;++ring)
            for(int i=0;i<steps;++i) {
                float a=i*2*pi/steps,b=(i+1)*2*pi/steps;
                auto side=[&](float angle) {
                    auto tangent=orbit_point(angle+.001f,ring)-orbit_point(angle-.001f,ring);
                    auto normal=glm::cross(tangent,toward);
                    if(glm::length(normal)<1e-7f) normal=right;
                    return glm::normalize(normal)*.036f*std::clamp(ring_width,.25f,3.f)*
                        ring_profile(angle,ring)*ease;
                };
                auto pa=orbit_point(a,ring),pb=orbit_point(b,ring);
                auto sa=side(a),sb=side(b);
                float brightness=.60f+.70f*ring_profile((a+b)*.5f,ring);
                quad(pa-sa,pb-sb,pb+sb,pa+sa,hue(a+ring*pi+clock*.2f)*brightness,0);
            }
        if(sparkles) for(int i=0;i<24;++i) {
            float a=i*2*pi/24+clock*(i%2?-.12f:.16f)+row*.7f;
            auto p=orbit_point(a,i%2);
            p.y+=.1f*std::sin(i*2.1f+clock*.3f);
            float pulse=.25f+.75f*std::pow(.5f+.5f*std::sin(clock*1.1f+i*2.3f),4.f);
            float size=(i%6==0?.12f:.055f)*ease;
            auto x=right*size,z=up*size;
            quad(p-x-z,p+x-z,p+x+z,p-x+z,hue(a+clock*.2f)*pulse,1);
        }
    }
    GLint buffer,src,dst,srca,dsta,func; GLboolean mask;
    bool blend=glIsEnabled(GL_BLEND),depth=glIsEnabled(GL_DEPTH_TEST),cull=glIsEnabled(GL_CULL_FACE);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&buffer);
    glGetIntegerv(GL_BLEND_SRC_RGB,&src);glGetIntegerv(GL_BLEND_DST_RGB,&dst);
    glGetIntegerv(GL_BLEND_SRC_ALPHA,&srca);glGetIntegerv(GL_BLEND_DST_ALPHA,&dsta);
    glGetIntegerv(GL_DEPTH_FUNC,&func);glGetBooleanv(GL_DEPTH_WRITEMASK,&mask);
    if(!vbo) glGenBuffers(1,&vbo);
    glBindBuffer(GL_ARRAY_BUFFER,vbo);
    glBufferData(GL_ARRAY_BUFFER,vertices.size()*sizeof(vertex_t),vertices.data(),GL_STREAM_DRAW);
    glEnable(GL_BLEND);glBlendFunc(GL_ONE,GL_ONE);
    glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LEQUAL);glDepthMask(GL_FALSE);glDisable(GL_CULL_FACE);
    program.use(wf::TEXTURE_TYPE_RGBA);
    program.uniformMatrix4f("VP",vp);program.uniform1f("intensity",intensity*ease);
    program.attrib_pointer("position",3,sizeof(vertex_t),(void*)offsetof(vertex_t,position));
    program.attrib_pointer("texcoord",2,sizeof(vertex_t),(void*)offsetof(vertex_t,uv));
    program.attrib_pointer("tint",3,sizeof(vertex_t),(void*)offsetof(vertex_t,tint));
    program.attrib_pointer("kind",1,sizeof(vertex_t),(void*)offsetof(vertex_t,kind));
    glDrawArrays(GL_TRIANGLES,0,vertices.size());program.deactivate();
    glBindBuffer(GL_ARRAY_BUFFER,buffer);glDepthFunc(func);glDepthMask(mask);
    glBlendFuncSeparate(src,dst,srca,dsta);
    if(!blend) glDisable(GL_BLEND);
    if(!depth) glDisable(GL_DEPTH_TEST);
    if(cull) glEnable(GL_CULL_FACE);
}
void cosmic_accents_t::release() {
    if(vbo) glDeleteBuffers(1,&vbo);
    vbo=0;program.free_resources();
}
