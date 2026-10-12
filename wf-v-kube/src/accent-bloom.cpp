#include "accent-bloom.hpp"
#include <algorithm>

static const char *bloom_vertex=R"(#version 100
attribute vec2 position;
varying highp vec2 uv;
void main(){uv=position*.5+.5;gl_Position=vec4(position,0,1);}
)";
static const char *bloom_extract=R"(#version 100
precision highp float;
varying highp vec2 uv;
uniform sampler2D original, accented;
uniform float threshold;
uniform vec2 texel;
vec3 sampleAccent(vec2 p) {
    // Positive change isolates the depth-tested additive effects. Desktop RGB
    // never enters the bloom source, even if the desktop is pure white.
    vec3 c=max(texture2D(accented,p).rgb-texture2D(original,p).rgb,vec3(0));
    float peak=max(c.r,max(c.g,c.b));
    return c*smoothstep(threshold,threshold+.15,peak);
}
void main(){
    vec3 c=sampleAccent(uv+texel*vec2(-.5,-.5))+sampleAccent(uv+texel*vec2(.5,-.5))+
        sampleAccent(uv+texel*vec2(-.5,.5))+sampleAccent(uv+texel*vec2(.5,.5));
    gl_FragColor=vec4(c*.25,0);
})";
static const char *bloom_blur=R"(#version 100
precision mediump float;
varying highp vec2 uv;
uniform sampler2D source;
uniform vec2 step_uv;
void main(){
    vec3 c=texture2D(source,uv).rgb*.227027;
    c+=(texture2D(source,uv+step_uv*1.384615).rgb+texture2D(source,uv-step_uv*1.384615).rgb)*.316216;
    c+=(texture2D(source,uv+step_uv*3.230769).rgb+texture2D(source,uv-step_uv*3.230769).rgb)*.070270;
    gl_FragColor=vec4(c,0);
})";
static const char *bloom_combine=R"(#version 100
precision mediump float;
varying highp vec2 uv;
uniform sampler2D source;
uniform float strength;
void main(){gl_FragColor=vec4(texture2D(source,uv).rgb*strength,0);}
)";
namespace {
const GLfloat quad[]={-1,-1,1,-1,-1,1,1,1};
void texture(GLuint id,int unit){
    glActiveTexture(GL_TEXTURE0+unit);glBindTexture(GL_TEXTURE_2D,id);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
}
void draw(OpenGL::program_t& program){
    glBindBuffer(GL_ARRAY_BUFFER,0);
    program.attrib_pointer("position",2,0,quad);
    glDrawArrays(GL_TRIANGLE_STRIP,0,4);program.deactivate();
}
}
void accent_bloom_t::snapshot(GLuint& id,int w,int h,bool resize){
    if(!id) glGenTextures(1,&id);
    texture(id,0);
    if(resize) glTexImage2D(GL_TEXTURE_2D,0,GL_RGB,w,h,0,GL_RGB,GL_UNSIGNED_BYTE,nullptr);
    glCopyTexSubImage2D(GL_TEXTURE_2D,0,0,0,0,0,w,h);
}
void accent_bloom_t::begin(const wf::render_target_t& target){
    wf::gles::bind_render_buffer(target);
    // Some render helpers only bind the draw FBO; copy the actual output.
    GLint framebuffer=0;glGetIntegerv(GL_FRAMEBUFFER_BINDING,&framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);
    auto size=target.get_size();
    bool resize=width!=size.width || height!=size.height || !before;
    width=size.width;height=size.height;
    snapshot(before,width,height,resize);
    if(resize){
        if(!after) glGenTextures(1,&after);
        texture(after,0);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGB,width,height,0,GL_RGB,GL_UNSIGNED_BYTE,nullptr);
    }
}
void accent_bloom_t::finish(const wf::render_target_t& target,float strength,float spread,float threshold,int quality){
    wf::gles::bind_render_buffer(target);
    GLint framebuffer=0;glGetIntegerv(GL_FRAMEBUFFER_BINDING,&framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);
    snapshot(after,width,height,false);
    if(!extract.get_program_id(wf::TEXTURE_TYPE_RGBA)){
        extract.set_simple(OpenGL::compile_program(bloom_vertex,bloom_extract));
        blur.set_simple(OpenGL::compile_program(bloom_vertex,bloom_blur));
        combine.set_simple(OpenGL::compile_program(bloom_vertex,bloom_combine));
    }
    int divisor=quality==0?4:2;
    wf::dimensions_t size{std::max(1,width/divisor),std::max(1,height/divisor)};
    ping.allocate(size);pong.allocate(size);
    glDisable(GL_SCISSOR_TEST);glDisable(GL_BLEND);glDisable(GL_DEPTH_TEST);glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    wf::gles::bind_render_buffer(ping.get_renderbuffer());
    extract.use(wf::TEXTURE_TYPE_RGBA);
    texture(before,0);texture(after,1);
    extract.uniform1i("original",0);extract.uniform1i("accented",1);
    extract.uniform1f("threshold",threshold);
    extract.uniform2f("texel",1.f/size.width,1.f/size.height);draw(extract);
    for(int pass=0;pass<4;++pass){
        auto& destination=pass%2?ping:pong;
        auto& source=pass%2?pong:ping;
        wf::gles::bind_render_buffer(destination.get_renderbuffer());
        blur.use(wf::TEXTURE_TYPE_RGBA);texture(wf::gles_texture_t::from_aux(source).tex_id,0);
        blur.uniform1i("source",0);
        float radius=spread*(pass<2?.45f:1.f);
        blur.uniform2f("step_uv",pass%2?0:radius/width,pass%2?radius/height:0);
        draw(blur);
    }
    wf::gles::bind_render_buffer(target);
    glEnable(GL_BLEND);glBlendFunc(GL_ONE,GL_ONE);
    combine.use(wf::TEXTURE_TYPE_RGBA);texture(wf::gles_texture_t::from_aux(ping).tex_id,0);
    combine.uniform1i("source",0);combine.uniform1f("strength",strength);draw(combine);
    glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,0);
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,0);
}
void accent_bloom_t::release(){
    if(before) glDeleteTextures(1,&before);
    if(after) glDeleteTextures(1,&after);
    before=after=0;width=height=0;ping.free();pong.free();
    extract.free_resources();blur.free_resources();combine.free_resources();
}
