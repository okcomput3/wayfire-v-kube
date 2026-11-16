#!/usr/bin/env python3
"""Exercise first-frame clock anchoring with deliberately slow frame setup."""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'wf-v-kube/src/cube.cpp').read_text()
start = source.index('    bool prepare_opening_frame()')
end = source.index('    void render(', start)
method = source[start:end]
easing = re.search(r'popout_scale_animation\{.*?(\[\] \(double t\) \{.*?\})', source, re.S)[1]
harness = r'''
#include <cassert>
#include <cmath>
static double now = 0;
namespace wf { namespace animation {
struct duration_t {
 double started = 0;
 int starts = 0;
 void start() { started = now; ++starts; }
};
}}
struct fixture {
 bool opening_frame_pending = true;
 struct { wf::animation::duration_t cube_animation; } animation;
 wf::animation::duration_t popout_scale_animation, panorama_animation;
 int view_updates = 0;
 void update_view_matrix() { ++view_updates; }
METHOD
};
int main() {
 auto easing = EASING;
 assert(easing(0) == 0 && easing(1) == 1);
 // Slow capture must not consume any of the opening animation.
 for(double setup_ms : {0., 50., 500.}) {
  fixture f;
  now = setup_ms;
  assert(f.prepare_opening_frame());
  assert(f.animation.cube_animation.started == now);
  assert(f.popout_scale_animation.started == now);
  assert(f.panorama_animation.started == now);
  assert(f.view_updates == 1);
  double previous = 1;
  for(int frame = 0; frame <= 18; ++frame) {
   now = setup_ms + frame * (300. / 18.);
   assert(!f.prepare_opening_frame());
   double t = (now - f.popout_scale_animation.started) / 300.;
   double scale = 1 + .2 * easing(t);
   assert(scale >= previous - 1e-8);
   if(frame == 0) assert(scale == 1);
   if(frame == 1) assert(scale < 1.003);
   if(frame == 18) assert(std::abs(scale-1.2) < 1e-8);
   previous = scale;
  }
  assert(f.popout_scale_animation.starts == 1);
  // Reopening anchors a fresh transition once, rather than every frame.
  f.opening_frame_pending = true;
  now += 500;
  assert(f.prepare_opening_frame());
  assert(f.popout_scale_animation.started == now);
  assert(f.panorama_animation.started == now);
 }
}
'''.replace('METHOD', method).replace('EASING', easing)
harness = '#include <initializer_list>\n' + harness
with tempfile.TemporaryDirectory() as tmp:
    cpp = Path(tmp) / 'animation.cpp'
    exe = Path(tmp) / 'animation'
    cpp.write_text(harness)
    subprocess.run(['c++', '-std=c++17', str(cpp), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('Popout opening: delayed setup, exact start, gentle first step, completion and reopening passed')
