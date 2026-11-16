#!/usr/bin/env python3
"""Check balanced depth-buffer ownership across repeated cube activations."""
from pathlib import Path
import subprocess
import tempfile
source = (Path(__file__).resolve().parents[1] / 'wf-v-kube/src/cube.cpp').read_text()

def block(marker):
    start = source.index(marker)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

harness = r'''
#include <cassert>
struct renderer {
    int requests=0, allocations=0, cached_width=0;
    bool texture=false;
    void set_require_depth_buffer(bool required) {
        requests += required ? 1 : -1;
        assert(requests>=0);
        if (!requests) texture=false;
    }
    void frame(int width) {
        // Mirrors the affected Wayfire allocator: freeing the texture does
        // not reset cached dimensions. Reopening must retain the request.
        if (requests && cached_width!=width) {
            texture=true; cached_width=width; ++allocations;
        }
        assert(requests==1 && texture);
    }
};
struct fixture {
    renderer render;
    struct output_t {renderer* render;} storage{&render};
    output_t* output=&storage;
    bool depth_buffer_requested=false;
    void activate() { ACQUIRE }
    void fini() { RELEASE }
};
int main() {
    fixture f;
    for (int i=0; i<20; ++i) {
        f.activate(); f.render.frame(1920);
    }
    assert(f.render.allocations==1);
    f.render.frame(2560);
    f.activate(); f.render.frame(2560);
    assert(f.render.allocations==2);
    f.fini();
    assert(f.render.requests==0 && !f.render.texture);
    f.fini(); // Cleanup is balanced even if called again.
    assert(f.render.requests==0);
}
'''.replace('ACQUIRE', block('if (!depth_buffer_requested)')).replace('RELEASE', block('if (depth_buffer_requested)'))
with tempfile.TemporaryDirectory() as tmp:
    cpp=Path(tmp)/'depth.cpp'
    cpp.write_text(harness)
    binary=Path(tmp)/'depth'
    subprocess.run(['c++', '-std=c++17', str(cpp), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print('Depth lifetime: repeated activation, output resizing and balanced final cleanup passed')
