#pragma once
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <map>
#include <memory>
class cube_glb_animation_t;
#include "obj-material.hpp"

struct cube_obj_vertex_t { glm::vec3 position, normal; glm::vec2 uv{0}; };
// CPU-only OBJ import. Output is centered and normalized to a unit bounding box.
bool load_cube_obj(const std::string& path, std::vector<cube_obj_vertex_t>& vertices,
    std::string& error);

struct cube_obj_batch_t { size_t first = 0, count = 0, material = 0; };
struct cube_model_image_t { int width, height; std::vector<unsigned char> rgba; };
struct cube_obj_mesh_t
{
    std::vector<cube_obj_vertex_t> vertices;
    std::vector<cube_obj_material_t> materials;
    std::vector<cube_obj_batch_t> batches;
    std::vector<std::string> warnings;
    std::map<std::string, cube_model_image_t> images;
    bool uv_top_left = false;
    std::shared_ptr<cube_glb_animation_t> animation;
};
bool load_cube_obj(const std::string& path, cube_obj_mesh_t& mesh, std::string& error);
