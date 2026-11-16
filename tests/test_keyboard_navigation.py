#!/usr/bin/env python3
"""Run the production keyboard handler with mocked compositor/animation edges.

No graphics session is needed. Compile and execute the unchanged move_vp body
so the assertions exercise its mode decisions and pending rotation targets.
"""
from pathlib import Path
import subprocess
import tempfile

source = (Path(__file__).resolve().parents[1] / 'wf-v-kube/src/cube.cpp').read_text()
start = source.index('    bool move_vp(int dir)')
end = source.index('    /* Initiate with an button grab. */', start)
handler = source[start:end]

harness = r'''
#include <cassert>
#include <cmath>
#include <vector>
struct transition {
    float end = 0;
    void restart_with_end(float value) { end = value; }
};
struct animation_t {
    transition rotation, zoom, offset_z, offset_y, ease_deformation;
    void start() {}
};
struct renderer { void schedule_redraw() {} };
struct output_t {
    bool active = false;
    renderer storage;
    renderer *render = &storage;
    bool is_plugin_active(const char*) { return active; }
};
struct fixture {
 std::vector<int> frame_pending_outputs;
    output_t storage;
    output_t *output = &storage;
    struct { const char *name = "cube"; } grab_interface;
    struct {
        animation_t cube_animation;
        bool in_exit = false;
        float side_angle = 1;
    } animation;
    bool popout = false;
    bool can_activate = true;
    bool reset_called = false;
    bool activate(bool interactive = true) {
        if (output->active) return true;
        if (!can_activate) return false;
        output->active = true;
        popout = interactive;
        return true;
    }
    void reset_attribs() { reset_called = true; }
    void update_view_matrix() {}
HANDLER
};
int main() {
    fixture desktop;
    assert(desktop.move_vp(1));
    assert(!desktop.popout && desktop.animation.in_exit && desktop.reset_called);
    assert(desktop.animation.cube_animation.rotation.end == -1);
    // Key-repeat queues another face without changing into interactive mode.
    assert(desktop.move_vp(1));
    assert(desktop.animation.in_exit && !desktop.popout);
    assert(desktop.animation.cube_animation.rotation.end == -2);
    assert(desktop.move_vp(-1));
    assert(desktop.animation.cube_animation.rotation.end == -1);

    fixture cube;
    cube.output->active = true;
    cube.popout = true;
    cube.animation.cube_animation.zoom.end = 2.5;
    cube.animation.cube_animation.offset_z.end = 4;
    cube.animation.cube_animation.offset_y.end = .75;
    cube.animation.cube_animation.ease_deformation.end = 1;
    // Start partway around a face after mouse input.
    cube.animation.cube_animation.rotation.end = -.3;
    assert(cube.move_vp(1));
    assert(!cube.animation.in_exit && cube.popout && !cube.reset_called);
    assert(cube.animation.cube_animation.rotation.end == -1);
    assert(cube.animation.cube_animation.zoom.end == 2.5);
    assert(cube.animation.cube_animation.offset_z.end == 4);
    assert(cube.animation.cube_animation.offset_y.end == .75);
    assert(cube.animation.cube_animation.ease_deformation.end == 1);
    assert(cube.move_vp(-1));
    assert(cube.animation.cube_animation.rotation.end == 0);
    assert(!cube.animation.in_exit);

    fixture closing;
    closing.output->active = true;
    closing.animation.in_exit = true;
    assert(closing.move_vp(-1));
    assert(closing.animation.in_exit && closing.reset_called);

    fixture refused;
    refused.can_activate = false;
    assert(!refused.move_vp(1));
    assert(!refused.output->active && !refused.reset_called);
}
'''.replace('HANDLER', handler)
with tempfile.TemporaryDirectory(prefix='v-kube-navigation-') as tmp:
    cpp = Path(tmp) / 'test.cpp'
    binary = Path(tmp) / 'test'
    cpp.write_text(harness)
    subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', str(cpp), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print('Keyboard navigation: desktop transition, persistent cube, repeated/reversed keys, and activation refusal passed')
