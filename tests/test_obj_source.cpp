#include "obj-source.hpp"
#include <cassert>
#include <fstream>
#include <unistd.h>
int main()
{
    char dir[] = "/tmp/cube-source-test-XXXXXX";
    assert(mkdtemp(dir));
    std::filesystem::path folder(dir);
    cube_obj_source_t source;
    assert(source.resolve(dir, true).empty());
    auto a = (folder / "a.obj").string(), b = (folder / "b.GLB").string();
    std::ofstream(a) << "# first";
    assert(source.resolve(dir, true) == a);
    assert(source.resolve(dir, true) == a);
    std::ofstream(b) << "# second";
    std::ofstream(folder / "ignore.mtl") << "# not a model";
    std::filesystem::create_directory(folder / "not-a-file.obj");
    for (int i = 0; i < 20; ++i)
    {
        auto previous = source.resolve(dir);
        auto selected = source.resolve(dir, true);
        assert(previous != selected);
        assert(selected == a || selected == b);
        for (int frame = 0; frame < 5; ++frame) assert(source.resolve(dir) == selected);
    }
    assert(source.resolve(a) == a);
    assert(source.resolve(a, true) == a);
    std::filesystem::remove(a); std::filesystem::remove(b);
    assert(source.resolve(dir, true).empty());
    std::filesystem::remove(folder / "ignore.mtl");
    std::filesystem::remove(folder / "not-a-file.obj");
    std::filesystem::remove(folder);
}
