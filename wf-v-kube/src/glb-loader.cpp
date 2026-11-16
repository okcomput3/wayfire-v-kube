#include "glb-loader.hpp"
#include "glb-animation.hpp"
#define CGLTF_IMPLEMENTATION
#include "third-party/cgltf.h"
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_MAX_DIMENSIONS 8192
#include "third-party/stb_image.h"
#include <glm/gtc/type_ptr.hpp>
#include <filesystem>
#include <memory>
#include <functional>
#include <algorithm>
#include <stdexcept>
#include <cmath>

bool load_cube_glb(const std::string& path, cube_obj_mesh_t& mesh, std::string& error)
{
    mesh = {}; error.clear();
    try
    {
        if (std::filesystem::file_size(path) > 128 * 1024 * 1024)
            throw std::runtime_error("GLB exceeds 128 MiB");
        cgltf_options options{};
        cgltf_data *raw = nullptr;
        if (cgltf_parse_file(&options, path.c_str(), &raw) != cgltf_result_success)
            throw std::runtime_error("Cannot parse GLB");
        std::shared_ptr<cgltf_data> data(raw, cgltf_free);
        if (data->file_type != cgltf_file_type_glb) throw std::runtime_error("Expected binary glTF (.glb)");
        size_t buffer_bytes = 0;
        for (size_t i = 0; i < data->buffers_count; ++i)
        {
            if (data->buffers[i].size > 128 * 1024 * 1024 - buffer_bytes)
                throw std::runtime_error("GLB buffers exceed 128 MiB");
            buffer_bytes += data->buffers[i].size;
        }
        if (cgltf_load_buffers(&options, data.get(), path.c_str()) != cgltf_result_success ||
            cgltf_validate(data.get()) != cgltf_result_success)
            throw std::runtime_error("Invalid or missing GLB buffer data");
        if (data->nodes_count > 10000 || data->materials_count > 4096 || data->images_count > 128)
            throw std::runtime_error("GLB exceeds scene limits");
        for (size_t i = 0; i < data->extensions_required_count; ++i)
        {
            std::string ext = data->extensions_required[i];
            if (ext != "KHR_materials_unlit" && ext != "KHR_texture_transform")
                throw std::runtime_error("Unsupported required GLB extension: " + ext);
        }
        auto texture = [&](const cgltf_texture_view& view, cube_obj_map_t& map)
        {
            if (!view.texture || !view.texture->image) return;
            if (view.texcoord != 0 || (view.has_transform && view.transform.has_texcoord && view.transform.texcoord != 0))
                throw std::runtime_error("GLB texture requires unsupported UV set");
            auto image = view.texture->image;
            map.path = "@glb-image-" + std::to_string(image - data->images);
            if (view.texture->sampler)
                map.clamp = view.texture->sampler->wrap_s == 33071 && view.texture->sampler->wrap_t == 33071;
            if (view.has_transform)
            {
                // Bake texture transform into the shared material map; no image rewriting.
                map.scale = glm::make_vec2(view.transform.scale);
                map.offset = glm::make_vec2(view.transform.offset);
                if (view.transform.rotation != 0) throw std::runtime_error("Rotated GLB texture transforms are not supported");
            }
            if (mesh.images.count(map.path)) return;
            int w=0, h=0, channels=0;
            unsigned char *pixels = nullptr;
            if (image->buffer_view)
            {
                auto bytes = cgltf_buffer_view_data(image->buffer_view);
                if (!bytes || image->buffer_view->size > 64 * 1024 * 1024)
                    throw std::runtime_error("Invalid embedded GLB image");
                if (!stbi_info_from_memory(bytes, image->buffer_view->size, &w, &h, &channels) ||
                    w <= 0 || h <= 0 || uint64_t(w)*h > 16777216)
                    throw std::runtime_error("Invalid or oversized GLB image");
                pixels = stbi_load_from_memory(bytes, image->buffer_view->size, &w, &h, &channels, 4);
            } else if (image->uri && std::string(image->uri).rfind("data:", 0) != 0)
            {
                std::string uri = image->uri;
                cgltf_decode_uri(uri.data()); uri.resize(std::strlen(uri.c_str()));
                auto filename = (std::filesystem::path(path).parent_path() / uri).string();
                if (!stbi_info(filename.c_str(), &w, &h, &channels) || w <= 0 || h <= 0 || uint64_t(w)*h > 16777216)
                    throw std::runtime_error("Cannot read GLB image: " + filename);
                pixels = stbi_load(filename.c_str(), &w, &h, &channels, 4);
            }
            if (!pixels) throw std::runtime_error("GLB image must be PNG/JPEG (embedded or external)");
            std::unique_ptr<unsigned char, decltype(&stbi_image_free)> owned(pixels, stbi_image_free);
            size_t total = size_t(w)*h*4;
            for (auto& entry : mesh.images) total += entry.second.rgba.size();
            if (total > 256 * 1024 * 1024) throw std::runtime_error("GLB decoded textures exceed 256 MiB");
            mesh.images[map.path] = {w, h, std::vector<unsigned char>(pixels, pixels + size_t(w)*h*4)};
        };
        mesh.materials.emplace_back();
        mesh.materials[0].diffuse = glm::vec3(1);
        for (size_t i=0; i<data->materials_count; ++i)
        {
            const auto& m=data->materials[i];
            cube_obj_material_t out;
            out.name = m.name ? m.name : "GLB material";
            out.diffuse = glm::vec3(1);
            if (m.has_pbr_metallic_roughness)
            {
                const auto& p=m.pbr_metallic_roughness;
                out.diffuse = glm::make_vec3(p.base_color_factor);
                out.opacity = p.base_color_factor[3];
                out.specular = glm::mix(glm::vec3(.04f), out.diffuse, p.metallic_factor);
                out.shininess = std::clamp(2.0f / std::max(.002f, p.roughness_factor*p.roughness_factor)-2, 1.0f, 1000.0f);
                texture(p.base_color_texture, out.diffuse_map);
            }
            out.emission = glm::make_vec3(m.emissive_factor);
            out.illumination = m.unlit ? 0 : 2;
            out.alpha_mode = m.alpha_mode == cgltf_alpha_mode_opaque ? 1 :
                (m.alpha_mode == cgltf_alpha_mode_mask ? 2 : 0);
            out.alpha_cutoff = m.alpha_cutoff;
            mesh.materials.push_back(out);
        }
        auto player = std::make_shared<cube_glb_animation_t>(data);
        player->initialize(mesh);
        if (data->animations_count) mesh.animation = player;
        mesh.uv_top_left = true;
        return true;
    } catch(const std::exception& e) {error=e.what();mesh={};return false;}
}
