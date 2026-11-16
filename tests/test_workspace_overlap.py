#!/usr/bin/env python3
"""Exercise the real window proxy with windows crossing workspace boundaries."""
from pathlib import Path
import tempfile
import subprocess
root=Path(__file__).resolve().parents[1]
source=(root/'wf-v-kube/src/cube.cpp').read_text()
a=source.index('class windows_only_workspace_node_t')
b=source.index('// Custom node that shows only desktop',a)
proxy=source[a:b]
def method(signature):
 start=proxy.index(signature); end=proxy.index('{',start)+1; depth=1
 while depth:
  depth+=(proxy[end]=='{')-(proxy[end]=='}');end+=1
 return proxy[start:end].replace(' override','')
code=r'''
#include <cassert>
#include <vector>
#include <algorithm>
namespace wf {
struct point_t {int x,y;}; struct geometry_t {int x,y,width,height;};
constexpr int WSET_SORT_STACKING=1,WSET_MAPPED_ONLY=2,WSET_EXCLUDE_MINIMIZED=4;
struct output_t;
namespace scene {using damage_callback=int;using render_instance_uptr=int;}
}
struct node {int id;void gen_render_instances(std::vector<int>& v,int,wf::output_t*){v.push_back(id);}};
struct view {node n;node* get_root_node(){return &n;}};
struct wset_t {wf::point_t current{1,1};view first{{0}},second{{1}};
std::vector<view*> get_views(int){return {&first,&second};}
wf::point_t get_current_workspace(){return current;}};
namespace wf {struct output_t {wset_t ws;wset_t* wset(){return &ws;}
geometry_t layout{200,100,100,100};
geometry_t get_layout_geometry(){return layout;}
geometry_t get_relative_geometry(){return {0,0,100,100};}};}
struct fixture {wf::output_t* output; wf::point_t workspace;
HANDLERS
};
int main(){
 wf::output_t output;
 for(int row=0;row<3;++row)for(int col=0;col<3;++col){
  fixture f{&output,{col,row}};
  std::vector<int> instances;f.gen_render_instances(instances,0,&output);
  assert((instances==std::vector<int>{0,1})); // no main-workspace filtering
  auto box=f.get_bounding_box();
  assert(box.x==(col-1)*100 && box.y==(row-1)*100);
  output.layout={-1920,600,100,100};
  auto relocated=f.get_bounding_box();
  assert(relocated.x==box.x && relocated.y==box.y);
 }
 // A window straddling four desktops: per-face clipping covers its whole
 // area exactly once; moving it only changes the clip, not instance membership.
 for(auto window : {wf::geometry_t{-20,-20,40,40}, wf::geometry_t{50,20,120,60}}){
  int total=0,faces=0;
  for(int row=0;row<3;++row)for(int col=0;col<3;++col){
   fixture f{&output,{col,row}};auto b=f.get_bounding_box();
   int w=std::max(0,std::min(window.x+window.width,b.x+b.width)-std::max(window.x,b.x));
   int h=std::max(0,std::min(window.y+window.height,b.y+b.height)-std::max(window.y,b.y));
   total+=w*h;faces+=(w*h>0);
  }
  assert(total==window.width*window.height);
  assert(faces>=2);
 }
}
'''.replace('HANDLERS',method('void gen_render_instances(')+'\n'+method('wf::geometry_t get_bounding_box()'))
compositor=(root/'wf-v-kube/src/popout-compositor.cpp').read_text()
assert 'get_view_main_workspace' not in proxy+compositor
assert 'overlap &= target.geometry;' in compositor
assert 'window_damage &= target.geometry;' in compositor
with tempfile.TemporaryDirectory() as tmp:
 cpp=Path(tmp)/'overlap.cpp';cpp.write_text(code);exe=Path(tmp)/'overlap'
 subprocess.run(['c++','-std=c++17',str(cpp),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
print('Workspace overlap: horizontal/vertical/corner portions, moving windows and stacking passed')
