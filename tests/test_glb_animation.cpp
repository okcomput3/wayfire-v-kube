#include "glb-loader.hpp"
#include "glb-animation.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
int main(int argc,char**argv)
{
    assert(argc==2);
    cube_obj_mesh_t mesh;std::string error;
    if(!load_cube_glb(argv[1],mesh,error)) {std::cerr<<error;return 1;}
    assert(mesh.animation && !mesh.images.empty());
    cgltf_options options{}; cgltf_data *raw=nullptr;
    assert(cgltf_parse_file(&options,argv[1],&raw)==cgltf_result_success);
    assert(cgltf_load_buffers(&options,raw,argv[1])==cgltf_result_success);
    std::unique_ptr<cgltf_data,decltype(&cgltf_free)> source(raw,cgltf_free);
    auto clips=mesh.animation->clips();
    assert(clips.size()==3);
    std::vector<cube_obj_vertex_t> a,b,c,rest;
    mesh.animation->sample(0, -1, rest);
    assert(rest.size()==mesh.vertices.size());
    for(size_t i=0;i<rest.size();++i) assert(glm::length(rest[i].position-mesh.vertices[i].position)<1e-5f);
    for(int clip=0;clip<3;++clip)
    {
        mesh.animation->sample(0,clip,a);
        mesh.animation->sample(.3,clip,b);
        mesh.animation->sample(.3,clip,c);
        float movement=0;
        for(size_t i=0;i<a.size();++i)
        {
            movement+=glm::length(a[i].position-b[i].position);
            assert(glm::length(b[i].position-c[i].position)<1e-6f);
            assert(std::isfinite(glm::length(b[i].position)));
            assert(std::abs(glm::length(b[i].normal)-1)<1e-4f);
            assert(b[i].uv==a[i].uv);
        }
        assert(movement>.1f);
        float start=INFINITY,end=-INFINITY;
        auto& anim=raw->animations[clip];
        for(size_t channel=0;channel<anim.channels_count;++channel) {
            auto input=anim.channels[channel].sampler->input;float first,last;
            assert(cgltf_accessor_read_float(input,0,&first,1));
            assert(cgltf_accessor_read_float(input,input->count-1,&last,1));
            start=std::min(start,first);end=std::max(end,last);
        }
        mesh.animation->sample(.3 + double(end-start),clip,c);
        for(size_t i=0;i<b.size();++i) assert(glm::length(b[i].position-c[i].position)<1e-5f);
        std::cout<<clips[clip]<<": skeletal movement "<<movement<<", stable UVs and normalized normals\n";
    }
    mesh.animation->sample(0,0,a);
    mesh.animation->sample(.9,2,b);
    mesh.animation->sample(0,0,c);
    for(size_t i=0;i<a.size();++i) assert(glm::length(a[i].position-c[i].position)<1e-6f);
    std::cout<<"Clip switching restores pose correctly\n";
}
