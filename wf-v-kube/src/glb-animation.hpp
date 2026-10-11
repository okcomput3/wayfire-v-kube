#pragma once
#include "obj-loader.hpp"
#include "third-party/cgltf.h"
#include <memory>

// Decoded GLB data and fixed normalization are retained; textures never reload per frame.
class cube_glb_animation_t
{
    std::shared_ptr<cgltf_data> data;
    std::vector<cgltf_node> rest_nodes;
    glm::vec3 center{0};
    float size = 1;
  public:
    explicit cube_glb_animation_t(std::shared_ptr<cgltf_data> data);
    void initialize(cube_obj_mesh_t& mesh);
    void sample(double seconds, int clip, std::vector<cube_obj_vertex_t>& vertices);
    std::vector<std::string> clips() const;
};
void cube_glb_geometry(cgltf_data *data, std::vector<cube_obj_vertex_t>& vertices,
    std::vector<cube_obj_batch_t>* batches = nullptr);
