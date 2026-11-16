#include "obj-loader.hpp"
#include <cassert>
#include <fstream>
#include <cmath>
#include <cstdio>
#include <unistd.h>
#include <filesystem>
int main()
{
    char path[] = "/tmp/cube-obj-test-XXXXXX";
    int fd = mkstemp(path); assert(fd >= 0); close(fd);
    std::vector<cube_obj_vertex_t> vertices;
    std::string error;
    auto load = [&](const char *text) {
        { std::ofstream file(path); file << text; }
        return load_cube_obj(path, vertices, error);
    };
    assert(load("v 10 0 0\nv 12 0 0\nv 12 2 0\nv 10 2 0\nvn 0 0 2\nf -4//1 -3//1 -2//1 -1//1\n"));
    assert(vertices.size() == 6);
    for (auto v : vertices) {
        assert(std::abs(v.position.x) <= .5f && std::abs(v.position.y) <= .5f);
        assert(std::abs(glm::length(v.normal) - 1) < 1e-6);
    }
    assert(load("v 0 0 0\nv 1 0 0\nv 0 1 0\nvt 0 0\nf 1/1 2/1 3/1 # face\n"));
    assert(vertices.size() == 3 && vertices[0].normal.z == 1);
    assert(!load("v 0 0 0\nf 1 2 3\n") && vertices.empty());
    assert(!load("v nan 0 0\n"));
    assert(!load("v 0 0 0\nf 0 0 0\n"));
    assert(!load("v 0 0 0\nf 1 1 1\n"));
    assert(!load("f -1 -2 -3\n"));
    assert(!load("# empty\n"));
    const std::string mtl = std::string(path) + ".mtl";
    {
        std::ofstream file(mtl);
        file << "newmtl Painted\nKd 0.2 0.4 0.6\nKs 1 0.5 0.1\nNs 80\n"
             << "map_Kd -s 2 3 1 -o 0.25 0.5 -clamp on \"textures/paint image.png\"\n"
             << "newmtl Glass\nTr 0.75\nmap_d masks/opacity.png\n";
    }
    {
        std::ofstream file(path);
        file << "v 0 0 0\nv 1 0 0\nv 0 1 0\nvt 0 0\nvt 1 0\nvt 0 1\n"
             << "usemtl Painted\nf 1/1 2/2 3/3\nusemtl Glass\nf -3/-3 -2/-2 -1/-1\n"
             << "mtllib " << std::filesystem::path(mtl).filename().string() << "\n";
    }
    cube_obj_mesh_t mesh;
    assert(load_cube_obj(path, mesh, error));
    assert(mesh.vertices.size() == 6 && mesh.batches.size() == 2);
    assert(mesh.batches[0].material == 1 && mesh.batches[1].material == 2);
    assert(mesh.vertices[1].uv == glm::vec2(1, 0));
    assert(mesh.vertices[5].uv == glm::vec2(0, 1));
    assert(mesh.materials[1].diffuse == glm::vec3(.2f, .4f, .6f));
    assert(mesh.materials[1].shininess == 80);
    assert(mesh.materials[1].diffuse_map.path == "/tmp/textures/paint image.png");
    assert(mesh.materials[1].diffuse_map.scale == glm::vec2(2, 3));
    assert(mesh.materials[1].diffuse_map.offset == glm::vec2(.25f, .5f));
    assert(mesh.materials[1].diffuse_map.clamp);
    assert(mesh.materials[2].opacity == .25f);
    assert(mesh.warnings.empty());
    std::remove(mtl.c_str());
    assert(load_cube_obj(path, mesh, error) && !mesh.warnings.empty());
    std::remove(path);
    assert(!load_cube_obj(path, vertices, error));
}
