#include "../wf-v-kube/src/thickness-visibility.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <cassert>
#include <iostream>
int main() {
    glm::mat4 identity(1);
    assert(thickness_visible(identity,identity,{-.5,-.5},{.5,.5},0,1,1,.1));
    auto off=glm::translate(identity,{0,5,.8660254f});
    for (int deform=0;deform<3;++deform)
        assert(!thickness_visible(identity,off,{-.5,-.5},{.5,.5},deform,1,1,.1));
    // Include the back plane even when the entire front is beyond the far plane.
    auto back=glm::translate(identity,{0,0,1.05});
    assert(thickness_visible(identity,back,{-.1,-.1},{.1,.1},0,1,1,.15));
    auto projection=glm::perspective(glm::radians(70.f),1.7f,.1f,30.f);
    auto vp=projection*glm::lookAt(glm::vec3(0,1,4),glm::vec3(0),glm::vec3(0,1,0));
    // No sampled visible extrusion point may belong to a culled tile.
    int rejected=0;
    for(int deformation=0;deformation<3;++deformation)
    for(int row=-8;row<9;++row)
    for(int face=0;face<6;++face)
    for(int tile=0;tile<8;++tile) {
        auto model=glm::translate(identity,{0,row*1.2f,0})*
            glm::rotate(identity,face*1.04719755f,glm::vec3(0,1,0))*
            glm::translate(identity,{0,0,.8660254f});
        glm::vec2 low(-.5f+tile*.125f,-.5f),high(low.x+.125f,.5f);
        if(thickness_visible(vp,model,low,high,deformation,.7f,1,.1f)) continue;
        ++rejected;
        for(int i=0;i<=8;++i) for(int j=0;j<=8;++j) {
            auto p=glm::vec3(model*glm::vec4(low.x+(high.x-low.x)*i/8, -.5f+j/8.f,0,1));
            float r=glm::length(glm::vec2(p.x,p.z));
            if(deformation) {float f=std::pow(deformation==1?.5f/r:r/.5f,.7f);p.x*=f;p.z*=f;}
            auto clip=vp*glm::vec4(p,1);
            assert(clip.w<0 || std::abs(clip.x)>clip.w || std::abs(clip.y)>clip.w || std::abs(clip.z)>clip.w);
        }
    }
    assert(rejected>1000);
    std::cout<<"PASS: conservative cube/cylinder/star culling; "<<rejected<<" off-screen tiles rejected\n";
}
