#!/usr/bin/env python3
"""Compile mirror ownership/geometry methods against fake output lifetimes."""
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
source=(root/'wf-v-kube/src/cube.cpp').read_text()
start=source.index('    wayfire_cube *controller()')
end=source.index('    void init() override',start)
methods=source[start:end]
code=r'''
#include <cassert>
#include <vector>
#include <algorithm>
namespace wf {
struct geometry_t { double x,y,width,height; };
struct renderer { int depth=0, damage=0;
 void set_require_depth_buffer(bool on) {depth+=on?1:-1;}
 void damage_whole() {++damage;}
};
struct output_t {
 geometry_t box; renderer r; renderer* render=&r;
 bool active=false, allow=true;
 geometry_t get_layout_geometry() {return box;}
 bool is_plugin_active(const char*) {return active;}
 template<class T> bool activate_plugin(T*) {if(!allow) return false; assert(!active); return active=true;}
 template<class T> void deactivate_plugin(T*) {assert(active); active=false;}
};
}
struct wayfire_cube {
 wf::output_t *output;
 inline static std::vector<wayfire_cube*> instances;
 wayfire_cube *mirrored_by=nullptr;
 std::vector<wayfire_cube*> mirrors;
 bool depth_buffer_requested=false;
 struct {const char* name="cube";} grab_interface;
METHODS
};
int main() {
 wf::output_t a{{0,0,1920,1080}}, b{{-1280,0,1280,1024}}, c{{1920,-200.5,2560,1440}};
 wayfire_cube x{&a},y{&b},z{&c};
 wayfire_cube::instances={&x,&y,&z};
 assert(x.acquire_mirrors());
 assert(y.controller()==&x && z.controller()==&x && x.controller()==&x);
 assert(x.shows_on(&a) && x.shows_on(&b) && x.shows_on(&c));
 auto bounds=x.mirror_geometry();
 assert(bounds.x==-1280 && bounds.y==-200.5 && bounds.width==5760 && bounds.height==1440);
 assert(b.r.depth==1 && c.r.depth==1);
 x.release_mirrors();
 assert(!b.active && !c.active && y.controller()==&y && z.controller()==&z);
 assert(x.mirrors.empty() && !x.shows_on(&b));
 // Reopening retains exactly one depth request per output.
 assert(x.acquire_mirrors()); x.release_mirrors();
 assert(b.r.depth==1 && c.r.depth==1);
 // A busy output must roll back already-acquired outputs without leaving locks.
 c.allow=false;
 assert(!x.acquire_mirrors());
 assert(!b.active && !c.active && x.mirrors.empty() && !y.mirrored_by);
 c.allow=true; b.active=true;
 assert(!x.acquire_mirrors()); assert(b.active); b.active=false;
 // Start from another monitor; it becomes the source, not always the first one.
 assert(z.acquire_mirrors());
 assert(x.controller()==&z && y.controller()==&z);
 z.release_mirrors();
 // A removed output no longer participates on subsequent activations.
 wayfire_cube::instances={&x,&y};
 assert(x.acquire_mirrors()); assert(x.mirrors.size()==1); x.release_mirrors();
}
'''.replace('METHODS',methods)
with tempfile.TemporaryDirectory() as tmp:
 cpp=Path(tmp)/'mirrors.cpp'; cpp.write_text(code); exe=Path(tmp)/'mirrors'
 subprocess.run(['c++','-std=c++17',str(cpp),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
print('Mirroring: source routing, mixed output layout, repeat activation, rollback and removal passed')
