#include "obj-material.hpp"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <cctype>

std::vector<std::string> cube_obj_tokens(const std::string& line)
{
    std::vector<std::string> result;
    std::string word;
    char quote = 0;
    for (char c : line)
    {
        if (quote)
        {
            if (c == quote) quote = 0; else word += c;
        } else if (c == '"' || c == '\'') quote = c;
        else if (c == '#') break;
        else if (std::isspace(static_cast<unsigned char>(c)))
        {
            if (!word.empty()) { result.push_back(word); word.clear(); }
        } else word += c;
    }
    if (!word.empty()) result.push_back(word);
    return result;
}

static float number(const std::string& s)
{
    size_t used;
    float v = std::stof(s, &used);
    if (used != s.size() || !std::isfinite(v)) throw std::runtime_error("Invalid MTL number");
    return v;
}

static cube_obj_map_t read_map(const std::vector<std::string>& tokens,
    const std::filesystem::path& parent)
{
    cube_obj_map_t map;
    size_t i = 1;
    while (i < tokens.size() && tokens[i][0] == '-')
    {
        auto flag = tokens[i++];
        if (flag == "-s" || flag == "-o" || flag == "-t")
        {
            glm::vec3 v(flag == "-s" ? 1.0f : 0.0f);
            int n = 0;
            while (i < tokens.size() && n < 3)
            {
                try { v[n] = number(tokens[i]); } catch (...) { break; }
                ++n; ++i;
            }
            if (!n) throw std::runtime_error("Missing MTL map transform");
            if (flag == "-s") map.scale = glm::vec2(v);
            if (flag == "-o") map.offset = glm::vec2(v);
        } else if (flag == "-clamp")
        {
            if (i == tokens.size()) throw std::runtime_error("Missing MTL clamp value");
            map.clamp = tokens[i++] == "on";
        } else if (flag == "-blendu" || flag == "-blendv" || flag == "-boost" ||
            flag == "-texres" || flag == "-bm" || flag == "-imfchan" || flag == "-type")
        {
            if (i == tokens.size()) throw std::runtime_error("Missing MTL map option");
            ++i;
        } else if (flag == "-mm")
        {
            if (i + 2 > tokens.size()) throw std::runtime_error("Missing MTL range values");
            i += 2;
        } else throw std::runtime_error("Unsupported MTL map option: " + flag);
    }
    std::string name;
    while (i < tokens.size()) { if (!name.empty()) name += ' '; name += tokens[i++]; }
    if (name.empty()) throw std::runtime_error("Missing MTL texture filename");
    std::replace(name.begin(), name.end(), '\\', '/');
    map.path = (parent / name).lexically_normal().string();
    return map;
}

void load_cube_mtl(const std::string& path, std::vector<cube_obj_material_t>& materials,
    std::vector<std::string>& warnings)
{
    std::ifstream file(path);
    if (!file) { warnings.push_back("Cannot open material library: " + path); return; }
    cube_obj_material_t *material = nullptr;
    std::string line;
    size_t bytes = 0;
    while (std::getline(file, line))
    {
        bytes += line.size();
        if (bytes > 8 * 1024 * 1024 || materials.size() >= 4096)
        { warnings.push_back("MTL exceeds import limits: " + path); break; }
        auto t = cube_obj_tokens(line);
        if (t.empty()) continue;
        try
        {
            if (t[0] == "newmtl")
            {
                if (t.size() < 2) throw std::runtime_error("Missing material name");
                std::string name = t[1];
                for (size_t i = 2; i < t.size(); ++i) name += " " + t[i];
                auto found = std::find_if(materials.begin(), materials.end(),
                    [&](const auto& m) { return m.name == name; });
                if (found == materials.end()) { materials.emplace_back(); material = &materials.back(); }
                else material = &*found;
                *material = cube_obj_material_t{};
                material->name = name;
                material->diffuse = glm::vec3(0.8f);
            } else if (material)
            {
                if (t[0] == "Kd" || t[0] == "Ka" || t[0] == "Ks" || t[0] == "Ke")
                {
                    if (t.size() < 4) throw std::runtime_error("Missing material RGB components");
                    glm::vec3 value(number(t[1]), number(t[2]), number(t[3]));
                    value = glm::clamp(value, glm::vec3(0), glm::vec3(1));
                    if (t[0] == "Kd") material->diffuse = value;
                    if (t[0] == "Ka") material->ambient = value;
                    if (t[0] == "Ks") material->specular = value;
                    if (t[0] == "Ke") material->emission = value;
                } else if (t[0] == "Ns" || t[0] == "d" || t[0] == "Tr" || t[0] == "illum")
                {
                    if (t.size() < 2) throw std::runtime_error("Missing material value");
                    float value = number(t.back());
                    if (t[0] == "Ns") material->shininess = std::clamp(value, 1.0f, 1000.0f);
                    if (t[0] == "d") material->opacity = std::clamp(value, 0.0f, 1.0f);
                    if (t[0] == "Tr") material->opacity = 1.0f - std::clamp(value, 0.0f, 1.0f);
                    if (t[0] == "illum") material->illumination = int(std::clamp(value, 0.0f, 10.0f));
                } else if (t[0] == "map_Kd") material->diffuse_map = read_map(t, std::filesystem::path(path).parent_path());
                else if (t[0] == "map_Ks") material->specular_map = read_map(t, std::filesystem::path(path).parent_path());
                else if (t[0] == "map_d") material->opacity_map = read_map(t, std::filesystem::path(path).parent_path());
            }
        } catch (const std::exception& e)
        {
            if (warnings.size() < 32) warnings.push_back(path + ": " + e.what());
        }
    }
}
