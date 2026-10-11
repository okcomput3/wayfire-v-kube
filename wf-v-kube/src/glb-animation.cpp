#include "glb-animation.hpp"
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/quaternion.hpp>
#include <functional>
#include <algorithm>
#include <cmath>
#include <stdexcept>

static glm::vec4 read(const cgltf_accessor *a, size_t i, int n)
{
    glm::vec4 v(0);
    if (!a || i >= a->count || !cgltf_accessor_read_float(a, i, glm::value_ptr(v), n))
        throw std::runtime_error("Invalid GLB animation/vertex accessor");
    for(int j=0;j<n;++j) if(!std::isfinite(v[j])) throw std::runtime_error("Non-finite GLB value");
    return v;
}

void cube_glb_geometry(cgltf_data *data, std::vector<cube_obj_vertex_t>& vertices,
    std::vector<cube_obj_batch_t>* batches)
{
    vertices.clear();
    if(batches) batches->clear();
    // Evaluate each node world matrix once, including joints outside the active scene.
    std::vector<glm::mat4> worlds(data->nodes_count);
    std::vector<int> state(data->nodes_count,0);
    std::function<glm::mat4(cgltf_node*,int)> world = [&](cgltf_node *node,int depth) {
        if(depth>128) throw std::runtime_error("GLB hierarchy too deep");
        auto idx=node-data->nodes;
        if(state[idx]==1) throw std::runtime_error("Cyclic GLB hierarchy");
        if(state[idx]==2) return worlds[idx];
        state[idx]=1;
        float values[16];cgltf_node_transform_local(node,values);
        auto matrix=glm::make_mat4(values);
        if(node->parent) matrix=world(node->parent,depth+1)*matrix;
        worlds[idx]=matrix;state[idx]=2;
        return matrix;
    };
    for(size_t i=0;i<data->nodes_count;++i) world(&data->nodes[i],0);
    size_t visited=0;
    std::function<void(cgltf_node*,unsigned)> visit = [&](cgltf_node *node,unsigned depth) {
        if(depth>128 || ++visited>10000) throw std::runtime_error("GLB scene recursion limit");
        auto matrix=worlds[node-data->nodes];
        std::vector<glm::mat4> joints;
        if(node->skin)
        {
            auto skin=node->skin;
            if(skin->joints_count>4096) throw std::runtime_error("GLB joint limit");
            for(size_t j=0;j<skin->joints_count;++j)
            {
                glm::mat4 bind(1);
                if(skin->inverse_bind_matrices && !cgltf_accessor_read_float(skin->inverse_bind_matrices,j,glm::value_ptr(bind),16))
                    throw std::runtime_error("Invalid inverse bind matrix");
                joints.push_back(worlds[skin->joints[j]-data->nodes]*bind);
            }
        }
        if(node->mesh) for(size_t p=0;p<node->mesh->primitives_count;++p)
        {
            auto& prim=node->mesh->primitives[p];
            if(prim.type!=cgltf_primitive_type_triangles || prim.has_draco_mesh_compression)
                throw std::runtime_error("GLB requires uncompressed triangles");
            const cgltf_accessor *pos=nullptr,*norm=nullptr,*uv=nullptr;
            const cgltf_accessor *joint_attrs[2]{},*weight_attrs[2]{};
            for(size_t a=0;a<prim.attributes_count;++a)
            {
                auto attr=prim.attributes[a];
                if(attr.type==cgltf_attribute_type_position) pos=attr.data;
                if(attr.type==cgltf_attribute_type_normal) norm=attr.data;
                if(attr.type==cgltf_attribute_type_texcoord && attr.index==0) uv=attr.data;
                if(attr.type==cgltf_attribute_type_joints && attr.index<2) joint_attrs[attr.index]=attr.data;
                if(attr.type==cgltf_attribute_type_weights && attr.index<2) weight_attrs[attr.index]=attr.data;
            }
            if(!pos) throw std::runtime_error("GLB missing positions");
            size_t count=prim.indices ? prim.indices->count : pos->count;
            if(count%3 || count>1500000-vertices.size()) throw std::runtime_error("GLB triangle limit");
            if(batches) batches->push_back({vertices.size(),count,prim.material ? size_t(prim.material-data->materials)+1 : 0});
            // Cache deformed indexed vertices once per primitive, then expand triangles.
            if(pos->count>1500000) throw std::runtime_error("GLB vertex limit");
            std::vector<cube_obj_vertex_t> cache(pos->count);
            for(size_t i=0;i<pos->count;++i)
            {
                auto position=glm::vec3(read(pos,i,3));
                auto normal=norm ? glm::vec3(read(norm,i,3)) : glm::vec3(0);
                const auto weights=node->weights ? node->weights : node->mesh->weights;
                size_t num_weights=node->weights ? node->weights_count : node->mesh->weights_count;
                for(size_t target=0;target<prim.targets_count && target<num_weights;++target)
                {
                    if(weights[target]==0) continue;
                    auto& morph=prim.targets[target];
                    for(size_t a=0;a<morph.attributes_count;++a)
                    {
                        auto attr=morph.attributes[a];
                        if(attr.type==cgltf_attribute_type_position) position+=weights[target]*glm::vec3(read(attr.data,i,3));
                        if(attr.type==cgltf_attribute_type_normal) normal+=weights[target]*glm::vec3(read(attr.data,i,3));
                    }
                }
                auto transform=matrix;
                if(!joints.empty() && joint_attrs[0] && weight_attrs[0])
                {
                    transform=glm::mat4(0);float sum=0;
                    for(int set=0;set<2;++set) if(joint_attrs[set] && weight_attrs[set])
                    {
                        auto ids=read(joint_attrs[set],i,4), weights=read(weight_attrs[set],i,4);
                        for(int j=0;j<4;++j) if(weights[j]>0)
                        {
                            if(ids[j]<0 || ids[j]>=joints.size()) throw std::runtime_error("Invalid GLB joint index");
                            transform+=joints[size_t(ids[j])]*weights[j];sum+=weights[j];
                        }
                    }
                    if(sum>1e-8f) transform/=sum;else transform=matrix;
                }
                auto& v=cache[i];
                v.position=glm::vec3(transform*glm::vec4(position,1));
                auto det=glm::determinant(glm::mat3(transform));
                v.normal=std::abs(det)>1e-10f ? glm::transpose(glm::inverse(glm::mat3(transform)))*normal : glm::vec3(0);
                if(uv) v.uv=glm::vec2(read(uv,i,2));
                for(int axis=0;axis<3;++axis) if(!std::isfinite(v.position[axis])) throw std::runtime_error("Invalid GLB transform");
            }
            for(size_t t=0;t<count;t+=3)
            {
                cube_obj_vertex_t triangle[3];
                for(int k=0;k<3;++k)
                {
                    size_t i=prim.indices ? cgltf_accessor_read_index(prim.indices,t+k) : t+k;
                    if(i>=cache.size()) throw std::runtime_error("Invalid GLB index");
                    triangle[k]=cache[i];
                }
                if(glm::determinant(glm::mat3(matrix))<0) std::swap(triangle[1],triangle[2]);
                auto normal=glm::cross(triangle[1].position-triangle[0].position,triangle[2].position-triangle[0].position);
                if(glm::length(normal)<1e-10f) normal={0,1,0};
                for(auto& v:triangle)
                {
                    if(!std::isfinite(glm::length(v.normal)) || glm::length(v.normal)<1e-10f) v.normal=normal;
                    v.normal=glm::normalize(v.normal);vertices.push_back(v);
                }
            }
        }
        for(size_t i=0;i<node->children_count;++i) visit(node->children[i],depth+1);
    };
    auto scene=data->scene ? data->scene : (data->scenes_count ? &data->scenes[0] : nullptr);
    if(scene) for(size_t i=0;i<scene->nodes_count;++i) visit(scene->nodes[i],0);
    else for(size_t i=0;i<data->nodes_count;++i) if(!data->nodes[i].parent) visit(&data->nodes[i],0);
}

cube_glb_animation_t::cube_glb_animation_t(std::shared_ptr<cgltf_data> value) : data(std::move(value))
{
    rest_nodes.assign(data->nodes,data->nodes+data->nodes_count);
}

void cube_glb_animation_t::initialize(cube_obj_mesh_t& mesh)
{
    cube_glb_geometry(data.get(),mesh.vertices,&mesh.batches);
    if(mesh.vertices.empty()) throw std::runtime_error("GLB has no triangles");
    auto lo=mesh.vertices[0].position,hi=lo;
    for(auto& v:mesh.vertices) {lo=glm::min(lo,v.position);hi=glm::max(hi,v.position);}
    auto extent=hi-lo;size=std::max({extent.x,extent.y,extent.z});center=(lo+hi)*.5f;
    if(size<1e-10f) throw std::runtime_error("GLB has zero size");
    for(auto& v:mesh.vertices) v.position=(v.position-center)/size;
}

std::vector<std::string> cube_glb_animation_t::clips() const
{
    std::vector<std::string> result;
    for(size_t i=0;i<data->animations_count;++i)
        result.push_back(data->animations[i].name ? data->animations[i].name : "Animation "+std::to_string(i));
    return result;
}

void cube_glb_animation_t::sample(double seconds,int clip,std::vector<cube_obj_vertex_t>& vertices)
{
    // Restore every property so switching clips never inherits the previous pose.
    std::copy(rest_nodes.begin(),rest_nodes.end(),data->nodes);
    struct restore_t {
        cgltf_data *data;
        const std::vector<cgltf_node>& nodes;
        ~restore_t() { std::copy(nodes.begin(), nodes.end(), data->nodes); }
    } restore{data.get(), rest_nodes};
    std::vector<std::vector<float>> morph_weights(data->nodes_count);
    if(clip>=0 && size_t(clip)<data->animations_count)
    {
        auto& animation=data->animations[clip];
        float start=INFINITY,end=-INFINITY;
        for(size_t c=0;c<animation.channels_count;++c)
        {
            auto input=animation.channels[c].sampler->input;
            if(!input->count) continue;
            start=std::min(start,read(input,0,1).x);end=std::max(end,read(input,input->count-1,1).x);
        }
        float time=std::isfinite(start) ? start : 0;
        if(end>start && std::isfinite(seconds)) time+=std::fmod(std::max(0.0,seconds),double(end-start));
        for(size_t c=0;c<animation.channels_count;++c)
        {
            auto& channel=animation.channels[c];auto node=channel.target_node;
            if(!node) continue;
            auto& sampler=*channel.sampler;auto input=sampler.input;
            if(!input->count) continue;
            size_t low=0,high=input->count;
            while(low<high) {auto mid=(low+high)/2;if(read(input,mid,1).x<=time) low=mid+1;else high=mid;}
            size_t a=low ? low-1 : 0,b=std::min(a+1,input->count-1);
            float ta=read(input,a,1).x,tb=read(input,b,1).x;
            float dt=tb-ta,u=dt>0 ? std::clamp((time-ta)/dt,0.0f,1.0f) : 0;
            bool cubic=sampler.interpolation==cgltf_interpolation_type_cubic_spline;
            bool weights=channel.target_path==cgltf_animation_path_type_weights;
            size_t components=channel.target_path==cgltf_animation_path_type_rotation ? 4 : 3;
            if(weights) components=node->mesh && node->mesh->primitives_count ? node->mesh->primitives[0].targets_count : 0;
            if(components>128) throw std::runtime_error("GLB morph limit");
            std::vector<float> value(components);
            auto get=[&](size_t key,int tangent,size_t component) {
                size_t index=key*(cubic ? 3 : 1)+(cubic ? tangent : 0);
                if(weights) return read(sampler.output,index*components+component,1).x;
                return read(sampler.output,index,components)[component];
            };
            for(size_t j=0;j<components;++j)
            {
                float va=get(a,1,j),vb=get(b,1,j);
                if(sampler.interpolation==cgltf_interpolation_type_step) value[j]=va;
                else if(cubic) value[j]=(2*u*u*u-3*u*u+1)*va+(u*u*u-2*u*u+u)*dt*get(a,2,j)+
                    (-2*u*u*u+3*u*u)*vb+(u*u*u-u*u)*dt*get(b,0,j);
                else value[j]=va*(1-u)+vb*u;
            }
            if(channel.target_path==cgltf_animation_path_type_rotation)
            {
                glm::quat q(value[3],value[0],value[1],value[2]);
                if(sampler.interpolation==cgltf_interpolation_type_linear)
                {
                    glm::quat qa(get(a,1,3),get(a,1,0),get(a,1,1),get(a,1,2));
                    glm::quat qb(get(b,1,3),get(b,1,0),get(b,1,1),get(b,1,2));
                    q=glm::slerp(glm::normalize(qa),glm::normalize(qb),u);
                }
                if(glm::length(q)<1e-8f) q=glm::quat(1,0,0,0);
                q=glm::normalize(q);std::copy(glm::value_ptr(q),glm::value_ptr(q)+4,node->rotation);node->has_rotation=1;
            } else if(channel.target_path==cgltf_animation_path_type_translation)
            {std::copy(value.begin(),value.end(),node->translation);node->has_translation=1;}
            else if(channel.target_path==cgltf_animation_path_type_scale)
            {std::copy(value.begin(),value.end(),node->scale);node->has_scale=1;}
            else if(weights)
            {
                auto& values=morph_weights[node-data->nodes];values=std::move(value);
                node->weights=values.data();node->weights_count=values.size();
            }
        }
    }
    try {cube_glb_geometry(data.get(),vertices);} catch(...) {std::copy(rest_nodes.begin(),rest_nodes.end(),data->nodes);throw;}
    std::copy(rest_nodes.begin(),rest_nodes.end(),data->nodes);
    for(auto& v:vertices) v.position=(v.position-center)/size;
}
