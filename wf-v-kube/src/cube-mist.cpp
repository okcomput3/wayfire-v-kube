#include "cube-mist.hpp"
#include <glm/gtc/matrix_inverse.hpp>
#include <vector>
#include <cmath>
#include <algorithm>
#include <cstddef>

static const char *mist_vertex = R"(
#version 100
attribute vec3 position;
attribute vec2 texcoord;
attribute float seed;
attribute float row_y;
uniform vec3 toward_camera;
uniform mat4 VP;
varying mediump vec2 uv;
varying mediump float phase;
varying highp float front_distance;
void main() {
    uv = texcoord;
    phase = seed;
    front_distance = dot(position-vec3(0,row_y,0),toward_camera);
    gl_Position = VP * vec4(position, 1.0);
}
)";
static const char *mist_fragment = R"(
#version 100
precision highp float;
varying mediump vec2 uv;
varying mediump float phase;
varying highp float front_distance;
uniform float time;
uniform float opacity;
float hash(vec2 p) {
    vec3 q = fract(vec3(p.xyx)*.1031);
    q += dot(q,q.yzx+33.33);
    return fract((q.x+q.y)*q.z);
}
float noise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f*f*f*(f*(f*6.0-15.0)+10.0);
    return mix(mix(hash(i),hash(i+vec2(1,0)),f.x),
        mix(hash(i+vec2(0,1)),hash(i+vec2(1,1)),f.x),f.y);
}
float turbulence(vec2 p) {
    float value=0.0, weight=.52;
    for (int i=0;i<5;++i) {
        value += weight*noise(p);
        p=mat2(.8,-.6,.6,.8)*p*2.03+vec2(7.1,3.7);
        weight *= .48;
    }
    return value;
}
void main() {
    // Mask every fragment, not just puff centres: large billboards cannot
    // spill across the camera-facing half when the cube tilts or rotates.
    float rear = 1.0-smoothstep(-.18,.02,front_distance);
    float r2 = dot(uv,uv);
    if (r2>=1.0 || rear<=0.0) discard;
    float edge = exp(-3.5*r2)*(1.0-smoothstep(.55,1.0,r2));
    vec2 p = uv*2.1 + vec2(phase,time*.09);
    vec2 warp = vec2(noise(p*.75+time*.035),noise(p*.75+vec2(17.0,-time*.04)));
    float cloud = turbulence(p+(warp-.5)*1.8);
    float alpha = rear*edge*smoothstep(.18,.75,cloud)*opacity;
    gl_FragColor = vec4(vec3(.65,.70,.77)*alpha,alpha);
}

)";

void cube_mist_t::render(const glm::mat4& vp, const glm::mat4& view,
    int rows, float spacing, int faces, int deformation, float rotation,
    float time, float activation, float density, float speed)
{
    if (activation <= .0001f || density <= 0 || faces < 2) return;
    if (!program.get_program_id(wf::TEXTURE_TYPE_RGBA))
        program.set_simple(OpenGL::compile_program(mist_vertex,mist_fragment));
    struct vertex_t { glm::vec3 position; glm::vec2 uv; float seed; float row_y; };
    std::vector<vertex_t> vertices;
    vertices.reserve(rows*48*6);
    // Output-independent orientation: adjacent panorama slices use identical quads.
    auto inverse = glm::inverse(view);
    auto right = glm::normalize(glm::vec3(inverse*glm::vec4(1,0,0,0)));
    auto up = glm::normalize(glm::vec3(inverse*glm::vec4(0,1,0,0)));
    constexpr float pi = 3.14159265358979323846f;
    float step = 2*pi/faces;
    float apothem = .5f/std::tan(pi/faces);
    float clock = time*speed;
    for (int row=0; row<rows; ++row)
        for (int i=0; i<48; ++i)
        {
            float angle = i*2*pi/48 + clock*.13f + row*.73f;
            float relative = std::remainder(angle-rotation,step);
            float radius = faces == 2 ? .5f : apothem/std::cos(relative);
            if (deformation && faces>2)
                radius *= std::pow(deformation==1 ? .5f/radius : radius/.5f,activation);
            radius += .18f + .08f*std::sin(clock*.31f+i*2.4f);
            glm::vec3 center(radius*std::sin(angle), row*spacing +
                .48f*std::sin(clock*.24f+i*2.1f+row), radius*std::cos(angle));
            float size = (.48f+.10f*std::sin(i*1.7f+clock*.17f))*activation;
            auto x=right*size, y=up*size;
            float seed = i*2.37f+row*3.71f;
            vertex_t a{center-x-y,{-1,-1},seed,row*spacing}, b{center+x-y,{1,-1},seed,row*spacing};
            vertex_t c{center+x+y,{1,1},seed,row*spacing}, d{center-x+y,{-1,1},seed,row*spacing};
            vertices.insert(vertices.end(),{a,b,c,a,c,d});
        }
    // All puffs share one tint, so alpha compositing between them is commutative.
    GLint buffer,src,dst,srca,dsta,func;
    GLboolean mask;
    bool blend=glIsEnabled(GL_BLEND), depth=glIsEnabled(GL_DEPTH_TEST), cull=glIsEnabled(GL_CULL_FACE);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&buffer);
    glGetIntegerv(GL_BLEND_SRC_RGB,&src); glGetIntegerv(GL_BLEND_DST_RGB,&dst);
    glGetIntegerv(GL_BLEND_SRC_ALPHA,&srca); glGetIntegerv(GL_BLEND_DST_ALPHA,&dsta);
    glGetIntegerv(GL_DEPTH_FUNC,&func); glGetBooleanv(GL_DEPTH_WRITEMASK,&mask);
    if (!vbo) glGenBuffers(1,&vbo);
    glBindBuffer(GL_ARRAY_BUFFER,vbo);
    glBufferData(GL_ARRAY_BUFFER,vertices.size()*sizeof(vertex_t),vertices.data(),GL_STREAM_DRAW);
    glEnable(GL_BLEND); glBlendFunc(GL_ONE,GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL); glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    program.use(wf::TEXTURE_TYPE_RGBA);
    program.uniformMatrix4f("VP",vp);
    auto toward_camera = glm::normalize(glm::vec3(inverse*glm::vec4(0,0,1,0)));
    program.uniform3f("toward_camera",toward_camera.x,toward_camera.y,toward_camera.z);
    program.uniform1f("time",clock);
    program.uniform1f("opacity",std::clamp(density,0.f,3.f)*activation*.23f);
    program.attrib_pointer("position",3,sizeof(vertex_t),(void*)offsetof(vertex_t,position));
    program.attrib_pointer("texcoord",2,sizeof(vertex_t),(void*)offsetof(vertex_t,uv));
    program.attrib_pointer("seed",1,sizeof(vertex_t),(void*)offsetof(vertex_t,seed));
    program.attrib_pointer("row_y",1,sizeof(vertex_t),(void*)offsetof(vertex_t,row_y));
    glDrawArrays(GL_TRIANGLES,0,vertices.size());
    program.deactivate();
    glBindBuffer(GL_ARRAY_BUFFER,buffer);
    glDepthFunc(func); glDepthMask(mask);
    glBlendFuncSeparate(src,dst,srca,dsta);
    if (!blend) glDisable(GL_BLEND);
    if (!depth) glDisable(GL_DEPTH_TEST);
    if (cull) glEnable(GL_CULL_FACE);
}
void cube_mist_t::release()
{
    if (vbo) glDeleteBuffers(1,&vbo);
    vbo=0;
    program.free_resources();
}
