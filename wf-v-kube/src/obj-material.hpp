#pragma once
#include <glm/glm.hpp>
#include <string>
#include <vector>

struct cube_obj_map_t
{
    std::string path;
    glm::vec2 scale{1}, offset{0};
    bool clamp = false;
};
struct cube_obj_material_t
{
    std::string name;
    glm::vec3 diffuse{0.65f, 0.75f, 0.9f}, ambient{0.2f}, specular{0}, emission{0};
    float shininess = 32, opacity = 1;
    int illumination = 2;
    int alpha_mode = 0; // 0: coverage, 1: opaque, 2: cutoff
    float alpha_cutoff = 0.5f;
    cube_obj_map_t diffuse_map, opacity_map, specular_map;
};
// Filenames may be quoted; backslashes in exporter paths are normalized.
std::vector<std::string> cube_obj_tokens(const std::string& line);
void load_cube_mtl(const std::string& path, std::vector<cube_obj_material_t>& materials,
    std::vector<std::string>& warnings);
