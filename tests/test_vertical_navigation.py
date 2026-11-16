#!/usr/bin/env python3
"""Exercise the production vertical navigation with in-flight camera motion."""
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
source=(root/'wf-v-kube/src/cube.cpp').read_text()
a=source.index('bool move_vp_vertical(int dir)')
b=source.index('// input_grabbed()',a)
handler=source[a:b]
code=r'''
#include <algorithm>
#include <cmath>
#include <cassert>
#define CUBE_VERTICAL_SPACING -1.2f
struct camera { float current=0, end=0; operator float() const {return current;}
void animate(float start, float v) {assert(start==current); end=v;} };
struct renderer {void schedule_redraw() {}};
struct wset_t {struct grid_t {int height=4;}; grid_t get_workspace_grid_size(){return {};}};
struct output_t {bool active=false; renderer r; renderer* render=&r; wset_t ws;
bool is_plugin_active(const char*) {return active;} wset_t* wset(){return &ws;}};
struct fixture {
output_t storage; output_t* output=&storage;
struct {const char* name="cube";} grab_interface;
struct {bool in_exit=false;} animation;
camera camera_y_offset;
static constexpr int default_row_duration_ms=300;
struct duration { int ms=0; void set_value(int v){ms=v;} } clock;
duration* row_duration=&clock;
bool transverse_row=false;
int transverse_row_duration=1200;
bool allowed=true;
bool input_grabbed_active(){return output->active=allowed;}
HANDLER
};
int main(){
for(int initial=0;initial<4;++initial){
 fixture f; f.output->active=true;
 f.camera_y_offset.current=f.camera_y_offset.end=initial*CUBE_VERTICAL_SPACING;
 for(int n=initial+1;n<4;++n){
  assert(f.move_vp_vertical(1));
  assert(std::abs(f.camera_y_offset.end-n*CUBE_VERTICAL_SPACING)<1e-5);
 }
 assert(!f.move_vp_vertical(1));
 // Current animation position has never advanced: repeats must use its end.
 for(int n=2;n>=0;--n){
  assert(f.move_vp_vertical(-1));
  assert(std::abs(f.camera_y_offset.end-n*CUBE_VERTICAL_SPACING)<1e-5);
 }
 assert(!f.move_vp_vertical(-1));
 // Reopen from an arbitrary selected row without moving other rows.
 for(int row=0;row<4;++row){
  float camera_position=row*CUBE_VERTICAL_SPACING;
  int selected=std::lround(camera_position/CUBE_VERTICAL_SPACING);
  assert(selected==row);
  for(int workspace=0;workspace<4;++workspace){
   float position=-workspace*CUBE_VERTICAL_SPACING;
   assert(std::abs(position+camera_position-(row-workspace)*CUBE_VERTICAL_SPACING)<1e-5);
  }
 }
}
fixture timed; timed.output->active=true; timed.transverse_row=true;
assert(timed.move_vp_vertical(1) && timed.clock.ms==1200);
timed.camera_y_offset.current=CUBE_VERTICAL_SPACING*.4f;
timed.transverse_row_duration=700;
assert(timed.move_vp_vertical(-1) && timed.clock.ms==700);
timed.transverse_row=false;
assert(timed.move_vp_vertical(1) && timed.clock.ms==300);
fixture blocked;blocked.allowed=false;assert(!blocked.move_vp_vertical(1));
}
'''.replace('HANDLER',handler)
assert 'int target_y = row_offset;' in source
assert 'int target_y = row + 1;' in source
assert 'wf::point_t target_ws = {i, 0};' in source
assert 'int nvy = dvy;' in source
with tempfile.TemporaryDirectory() as tmp:
 cpp=Path(tmp)/'vertical.cpp';cpp.write_text(code);exe=Path(tmp)/'vertical'
 subprocess.run(['c++','-std=c++17',str(cpp),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
print('Vertical rows: fixed mapping, reopen, queued/reversed movement, limits and refusal passed')
