#!/usr/bin/env python3
"""Exercise the actual scroll handlers without a running compositor."""
from pathlib import Path
import subprocess
import tempfile

source = (Path(__file__).resolve().parents[1] / 'wf-v-kube/src/cube.cpp').read_text()

def method(signature):
    start = source.index(signature)
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end].replace(' override', '')

harness = r'''
#include "scroll-momentum.hpp"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
#define ZOOM_MIN 0.1f
#define ZOOM_MAX 10.0f
constexpr int WL_POINTER_AXIS_VERTICAL_SCROLL=0, WL_POINTER_AXIS_SOURCE_WHEEL=0, WL_POINTER_AXIS_SOURCE_FINGER=1;
struct wlr_pointer_axis_event { int orientation=0, source=0; double delta=0; int delta_discrete=0; unsigned time_msec=0; };
struct transition {
    float current=1, end=1;
    operator float() const { return current; }
    void set(float a, float b) { current=a; end=b; }
    void restart_with_end(float b) { end=b; }
};
struct fixture {
    double ZVelocity=.07;
    scroll_momentum_t scroll_momentum;
    struct { bool in_exit=false; struct {
        transition zoom, offset_y, rotation, ease_deformation, offset_z;
        void start() {}
    } cube_animation; } animation;
    struct renderer { int frames=0; void schedule_redraw() { ++frames; } } render;
    struct output_t { renderer* render; } storage{&render};
    output_t* output=&storage;
HANDLERS
};
int main() {
    fixture glide;
    glide.handle_pointer_axis({0, 1, 2, 0, 100});
    glide.handle_pointer_axis({0, 1, 2, 0, 116});
    glide.handle_pointer_axis({0, 1, 2, 0, 132});
    const float before_lift = glide.animation.cube_animation.zoom.end;
    glide.handle_pointer_axis({0, 1, 0, 0, 140});
    assert(glide.animation.cube_animation.zoom.end > before_lift);
    const float after_lift = glide.animation.cube_animation.zoom.end;
    glide.handle_pointer_axis({0, 1, 0, 0, 145});
    assert(glide.animation.cube_animation.zoom.end == after_lift);
    glide.handle_pointer_axis({0, 1, -2, 0, 200});
    glide.handle_pointer_axis({0, 1, -2, 0, 216});
    const float before_reverse_lift = glide.animation.cube_animation.zoom.end;
    glide.handle_pointer_axis({0, 1, 0, 0, 220});
    assert(glide.animation.cube_animation.zoom.end < before_reverse_lift);
    glide.handle_pointer_axis({0, 1, 2, 0, 300});
    glide.handle_pointer_axis({0, 1, 2, 0, 316});
    const float paused = glide.animation.cube_animation.zoom.end;
    glide.handle_pointer_axis({0, 1, 0, 0, 600});
    assert(glide.animation.cube_animation.zoom.end == paused);
    scroll_momentum_t motion;
    motion.update(2, 100);
    motion.update(2, 116);
    motion.update(-2, 132);
    assert(motion.update(0, 140) < 0);
    motion.update(2, 200);
    motion.update(2, 216);
    motion.reset();
    assert(motion.update(0, 220) == 0);
    fixture wheel, finger;
    wheel.animation.cube_animation.offset_z.end=4;
    wheel.handle_pointer_axis({0, 0, 15, 120});
    assert(wheel.animation.cube_animation.zoom.end>1);
    assert(wheel.animation.cube_animation.offset_z.end==4);
    // The same movement split into small finger events has the same result,
    // even when none of those events has had a chance to render yet.
    for(int i=0;i<30;++i) finger.handle_pointer_axis({0, 1, .5, 0});
    assert(std::abs(finger.animation.cube_animation.zoom.end-wheel.animation.cube_animation.zoom.end)<1e-5);
    wheel.handle_pointer_axis({0, 0, -15, -120});
    assert(std::abs(wheel.animation.cube_animation.zoom.end-1)<1e-5);
    fixture discrete;
    discrete.handle_pointer_axis({0, 0, 0, 120});
    assert(discrete.animation.cube_animation.zoom.end>1);
    int frames=discrete.render.frames;
    discrete.handle_pointer_axis({0, 1, 0, 120}); // finger stop, not a wheel tick
    discrete.handle_pointer_axis({1, 1, 15, 0}); // horizontal motion
    discrete.pointer_scrolled(std::numeric_limits<double>::quiet_NaN());
    assert(discrete.render.frames==frames);
    discrete.pointer_scrolled(1e9);
    assert(discrete.animation.cube_animation.zoom.end==ZOOM_MAX);
    discrete.pointer_scrolled(-1e9);
    assert(discrete.animation.cube_animation.zoom.end==ZOOM_MIN);
    discrete.animation.in_exit=true;
    frames=discrete.render.frames;
    discrete.pointer_scrolled(15);
    assert(discrete.render.frames==frames);
}
'''.replace('HANDLERS', method('void pointer_scrolled(double amount)') + '\n' + method('void handle_pointer_axis('))
# The controller must receive scrolls across output boundaries and consume the
# core event to prevent the local grab from applying the same delta twice.
assert 'wf::get_core().connect(&on_axis_event)' in method('bool activate(')
assert 'on_axis_event.disconnect()' in method('void deactivate()')
axis_start = source.index('>> on_axis_event =')
axis_end = source.index('};', axis_start)
axis_handler = source[axis_start:axis_end]
assert 'handle_pointer_axis(*ev->event)' in axis_handler
assert 'ev->mode = wf::input_event_processing_mode_t::IGNORE' in axis_handler

# Teardown must not issue a draw/clear against an unspecified framebuffer.
teardown = method('void deactivate()')
assert 'glClear' not in teardown
assert 'set_require_depth_buffer' not in teardown
with tempfile.TemporaryDirectory() as tmp:
    cpp = Path(tmp) / 'scroll.cpp'
    binary = Path(tmp) / 'scroll'
    cpp.write_text(harness)
    subprocess.run(['c++', '-std=c++17', '-I', str(Path(__file__).resolve().parents[1] / 'wf-v-kube/src'), str(cpp), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print('Scroll zoom: finger momentum, reversal, pause/reset, wheel, finger accumulation, reversal, v120 fallback, limits, stop/exit and depth cleanup passed')
