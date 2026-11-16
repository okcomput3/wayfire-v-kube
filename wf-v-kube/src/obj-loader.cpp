#include "obj-loader.hpp"
#include <fstream>
#include <sstream>
#include <cmath>
#include <algorithm>
#include <filesystem>
#include <stdexcept>

bool load_cube_obj(const std::string& path, cube_obj_mesh_t& mesh,
    std::string& error)
{
    mesh = {};
    mesh.materials.emplace_back();
    auto& vertices = mesh.vertices;
    std::string active_material;
    std::vector<std::string> batch_materials;
    std::vector<glm::vec2> uvs;
    error.clear();
    std::ifstream file(path);
    if (!file) { error = "Cannot open OBJ: " + path; return false; }
    std::vector<glm::vec3> positions, normals;
    struct index_t { int p, n, uv; };
    auto index = [] (const std::string& s, size_t count) {
        size_t used = 0;
        const long v = std::stol(s, &used);
        const long resolved = v > 0 ? v - 1 : long(count) + v;
        if (used != s.size() || v == 0 || resolved < 0 || resolved >= long(count))
            throw std::runtime_error("Invalid OBJ index");
        return int(resolved);
    };
    size_t bytes = 0;
    try
    {
        std::string line;
        while (std::getline(file, line))
        {
            bytes += line.size();
            if (bytes > 64 * 1024 * 1024 || positions.size() > 1000000 || normals.size() > 1000000 || uvs.size() > 1000000)
                throw std::runtime_error("OBJ exceeds import limits");
            std::istringstream in(line);
            std::string type; in >> type;
            if (type == "mtllib")
            {
                auto libraries = cube_obj_tokens(line);
                // Several exporters leave spaces in a single library name unquoted.
                if (libraries.size() > 2)
                {
                    std::string joined = libraries[1];
                    for (size_t i = 2; i < libraries.size(); ++i) joined += " " + libraries[i];
                    std::replace(joined.begin(), joined.end(), '\\', '/');
                    std::error_code ec;
                    if (std::filesystem::is_regular_file(std::filesystem::path(path).parent_path() / joined, ec))
                        libraries = {"mtllib", joined};
                }
                for (size_t i = 1; i < libraries.size(); ++i)
                {
                    std::replace(libraries[i].begin(), libraries[i].end(), '\\', '/');
                    load_cube_mtl((std::filesystem::path(path).parent_path() / libraries[i]).string(),
                        mesh.materials, mesh.warnings);
                }
            } else if (type == "usemtl")
            {
                auto names = cube_obj_tokens(line);
                active_material.clear();
                for (size_t i = 1; i < names.size(); ++i)
                { if (i > 1) active_material += ' '; active_material += names[i]; }
            } else if (type == "vt")
            {
                glm::vec2 uv(0);
                if (!(in >> uv.x)) throw std::runtime_error("Invalid OBJ texture coordinate");
                if (!(in >> uv.y)) uv.y = 0;
                if (!std::isfinite(uv.x) || !std::isfinite(uv.y))
                    throw std::runtime_error("Invalid OBJ texture coordinate");
                uvs.push_back(uv);
            } else if (type == "v" || type == "vn")
            {
                glm::vec3 p;
                if (!(in >> p.x >> p.y >> p.z) || !std::isfinite(p.x) ||
                    !std::isfinite(p.y) || !std::isfinite(p.z) || glm::length(p) > 1e12f)
                    throw std::runtime_error("Invalid OBJ vector");
                (type == "v" ? positions : normals).push_back(p);
            } else if (type == "f")
            {
                std::vector<index_t> face;
                std::string token;
                while (in >> token && token[0] != '#')
                {
                    auto slash = token.find('/');
                    auto last = slash == std::string::npos ? slash : token.find('/', slash + 1);
                    face.push_back({index(token.substr(0, slash), positions.size()),
                        last == std::string::npos || last + 1 == token.size() ? -1 :
                        index(token.substr(last + 1), normals.size()),
                        slash == std::string::npos || slash + 1 == token.size() || last == slash + 1 ? -1 :
                        index(token.substr(slash + 1, last == std::string::npos ? last : last - slash - 1), uvs.size())});
                    if (face.size() > 4096) throw std::runtime_error("OBJ polygon too large");
                }
                if (face.size() < 3) throw std::runtime_error("OBJ face has fewer than three vertices");
                if (mesh.batches.empty() || batch_materials.back() != active_material)
                {
                    mesh.batches.push_back({vertices.size(), 0, 0});
                    batch_materials.push_back(active_material);
                }
                // OBJ exporters normally supply triangles or convex quads. Triangulate their fan.
                for (size_t i = 1; i + 1 < face.size(); ++i)
                {
                    auto n = glm::cross(positions[face[i].p] - positions[face[0].p],
                        positions[face[i + 1].p] - positions[face[0].p]);
                    if (glm::length(n) < 1e-12f) continue;
                    n = glm::normalize(n);
                    for (size_t corner : {size_t(0), i, i + 1})
                    {
                        auto entry = face[corner];
                        auto normal = entry.n >= 0 ? normals[entry.n] : n;
                        if (glm::length(normal) < 1e-12f) normal = n;
                        vertices.push_back({positions[entry.p], glm::normalize(normal),
                            entry.uv >= 0 ? uvs[entry.uv] : glm::vec2(0)});
                        ++mesh.batches.back().count;
                    }
                    if (vertices.size() > 1500000) throw std::runtime_error("OBJ exceeds 500,000 triangles");
                }
            }
        }
        if (vertices.empty()) throw std::runtime_error("OBJ has no usable faces");
        glm::vec3 lo = vertices[0].position, hi = lo;
        for (auto& v : vertices) { lo = glm::min(lo, v.position); hi = glm::max(hi, v.position); }
        auto extent = hi - lo;
        float size = std::max({extent.x, extent.y, extent.z});
        if (size < 1e-12f) throw std::runtime_error("OBJ has zero size");
        for (auto& v : vertices) v.position = (v.position - (lo + hi) * 0.5f) / size;
        for (size_t i = 0; i < mesh.batches.size(); ++i)
        {
            auto found = std::find_if(mesh.materials.begin(), mesh.materials.end(),
                [&](const auto& m) { return m.name == batch_materials[i]; });
            if (found != mesh.materials.end()) mesh.batches[i].material = found - mesh.materials.begin();
            else if (mesh.warnings.size() < 32)
                mesh.warnings.push_back("Unknown material: " + batch_materials[i]);
        }
        return true;
    } catch (const std::exception& e)
    {
        error = e.what(); vertices.clear(); return false;
    }
}

bool load_cube_obj(const std::string& path, std::vector<cube_obj_vertex_t>& vertices,
    std::string& error)
{
    cube_obj_mesh_t mesh;
    bool result = load_cube_obj(path, mesh, error);
    vertices = std::move(mesh.vertices);
    return result;
}
