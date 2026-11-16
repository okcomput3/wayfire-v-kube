#pragma once
#include <wayfire/opengl.hpp>
class accent_bloom_t
{
    GLuint before=0, after=0;
    int width=0,height=0;
    wf::auxilliary_buffer_t ping,pong;
    OpenGL::program_t extract,blur,combine;
    void snapshot(GLuint& texture,int w,int h,bool resize);
  public:
    void begin(const wf::render_target_t& target);
    void finish(const wf::render_target_t& target,float strength,float spread,float threshold,int quality);
    void release();
};
