#!/usr/bin/env python3
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
code=r'''
#include "surface-order.hpp"
#include <cassert>
#include <iostream>
#include <glm/gtc/matrix_transform.hpp>
int main() {
 // Deliberately misleading center depths: side would previously paint over cap.
 std::vector<cube_surface_bounds_t> pair{{-.5,.5,.2,false},{.5,.5,.8,true}};
 assert((cube_surface_order(pair,2)==std::vector<int>{0,1}));
 assert((cube_surface_order(pair,-2)==std::vector<int>{1,0}));
 // Side-on transparent cube: the eye is inside the face's vertical extent.
 // Its bottom cap must paint first even if center depth says otherwise.
 std::vector<cube_surface_bounds_t> bottom{{-.5,.5,.8,false},{-.5,-.5,.2,true}};
 assert((cube_surface_order(bottom,0)==std::vector<int>{1,0}));
 std::vector<cube_surface_bounds_t> top{{-.5,.5,.8,false},{.5,.5,.2,true}};
 assert((cube_surface_order(top,0)==std::vector<int>{1,0}));
 // Check the compositing consequence: the face attenuates the cap, rather
 // than allowing the cap's full contribution to show through the face.
 float red=0, blue=0;
 for(int index : cube_surface_order(bottom,0)) {
  const float alpha=index==0 ? .75f : .8f;
  red=red*(1-alpha)+(index==0 ? alpha : 0);
  blue=blue*(1-alpha)+(index==1 ? alpha : 0);
 }
 assert(std::abs(red-.75f)<1e-5f && std::abs(blue-.2f)<1e-5f);
 for(float eye : {-4.f,-.1f,.7f,2.f,7.f}) {
  std::vector<cube_surface_bounds_t> geometry;
  for(int row=0;row<4;++row) {
   float y=row*1.2f;
   geometry.push_back({y-.5f,y+.5f,.8f-row*.1f,false});
   geometry.push_back({y-.6f,y+.6f,.2f+row*.1f,false});
   geometry.push_back({y-.5f,y-.5f,.5f,true});
   geometry.push_back({y+.5f,y+.5f,.6f,true});
  }
  auto order=cube_surface_order(geometry,eye);
  std::vector<int> pos(order.size());
  for(unsigned i=0;i<order.size();++i) pos[order[i]]=i;
  for(unsigned c=0;c<geometry.size();++c) if(geometry[c].cap) {
   float p=geometry[c].min_y;
   for(unsigned j=0;j<geometry.size();++j) {
    if(c==j) continue;
    auto b=geometry[j];
    if(std::abs(eye-p)<1e-5f) continue;
    if(b.min_y>=p-1e-5f && eye>p && b.min_y>=eye) continue;
    if(b.max_y<=p+1e-5f && eye<p && b.max_y<=eye) continue;
    if(b.min_y>=p-1e-5f && b.max_y>p+1e-5f)
     assert((pos[c]<pos[j])==(eye>p));
    if(b.max_y<=p+1e-5f && b.min_y<p-1e-5f)
     assert((pos[c]<pos[j])==(eye<p));
   }
  }
 }

 // Validate actual overlapping rays, independently of the sorter's constraints.
 // Includes nested window shells and stacked caps, with misleading centres.
 for(int faces : {3,4,6}) for(float rotation : {0.f,.37f,.81f})
 for(float eye_height : {-.8f,0.f,.7f,1.2f,2.9f,4.f}) {
  std::vector<cube_surface_bounds_t> geometry;
  glm::vec3 eye(0,eye_height,3);
  float radius=.5f/std::tan(3.14159265f/faces);
  for(int row=0;row<3;++row) {
   float y=1.2f*row;
   for(float scale : {1.f,1.2f}) for(int face=0;face<faces;++face) {
    float angle=rotation+face*6.2831853f/faces;
    glm::vec3 normal(std::sin(angle),0,std::cos(angle));
    glm::vec3 tangent(std::cos(angle),0,-std::sin(angle));
    glm::vec3 center=normal*(radius*scale)+glm::vec3(0,y,0);
    cube_surface_bounds_t b{y-scale*.5f,y+scale*.5f,-center.z,false};
    b.plane=glm::vec4(normal,-glm::dot(normal,center));
    for(auto uv : {glm::vec2(-.5,-.5),glm::vec2(.5,-.5),glm::vec2(.5,.5),glm::vec2(-.5,.5)})
     b.outline.push_back(center+scale*(uv.x*tangent+glm::vec3(0,uv.y,0)));
    geometry.push_back(b);
   }
   for(float offset : {-.5f,.5f}) {
    cube_surface_bounds_t b{y+offset,y+offset,0,true};
    b.plane=glm::vec4(0,1,0,-y-offset);
    for(int face=0;face<faces;++face) {
     float a=rotation+face*6.2831853f/faces;
     b.outline.push_back(glm::vec3(std::sin(a)*radius-std::cos(a)*.5f,y+offset,
       std::cos(a)*radius+std::sin(a)*.5f));
    }
    geometry.push_back(b);
   }
  }
  auto order=cube_planar_order(geometry,eye);
  // Splitting must preserve every source polygon's area (no holes or duplicates).
  auto area=[] (const std::vector<glm::vec3>& polygon) {
   float result=0;
   for(unsigned k=1;k+1<polygon.size();++k)
    result+=glm::length(glm::cross(polygon[k]-polygon[0],polygon[k+1]-polygon[0]))*.5f;
   return result;
  };
  std::vector<float> areas(geometry.size(),0);
  for(auto& piece : order) areas[piece.surface]+=area(piece.outline);
  for(unsigned k=0;k<geometry.size();++k)
   assert(std::abs(areas[k]-area(geometry[k].outline))<1e-4f);
  for(int px=-20;px<=20;++px) for(int py=-15;py<=40;++py) {
   glm::vec3 ray=glm::vec3(px*.08f,py*.08f,0)-eye;
   float last=INFINITY; int last_index=-1;
   for(auto& piece : order) {
    int index=piece.surface;
    auto b=geometry[index]; b.outline=piece.outline; auto n=glm::vec3(b.plane);
    float denominator=glm::dot(n,ray);
    if(std::abs(denominator)<1e-5f) continue;
    float t=-(glm::dot(n,eye)+b.plane.w)/denominator;
    if(t<=0) continue;
    auto point=eye+t*ray;
    bool positive=false,negative=false,boundary=false;
    for(unsigned k=0;k<b.outline.size();++k) {
     auto a=b.outline[k],c=b.outline[(k+1)%b.outline.size()];
     float d=glm::dot(glm::cross(c-a,point-a),n);
     positive|=d>1e-5f; negative|=d< -1e-5f; boundary|=std::abs(d)<1e-5f;
    }
    if((positive&&negative)||boundary) continue;
    if(t>last+1e-4f) { std::cerr << "faces " << faces << " rotation " << rotation << " eye " << eye_height << " pixel " << px << "," << py << " order " << last_index << " then " << index << " depth " << last << " then " << t << std::endl; std::abort(); }
    last=t; last_index=index;
   }
  }
 }
 // The eye extraction used by production must survive tilt/zoom/row offsets.
 for(float tilt : {-.9f,0.f,.9f}) for(float zoom : {.5f,1.f,3.f}) {
  auto view=glm::translate(glm::mat4(1),glm::vec3(0,0,-3));
  view*=glm::rotate(glm::mat4(1),tilt,glm::vec3(1,0,0));
  view*=glm::translate(glm::mat4(1),glm::vec3(0,-2.4,0));
  view*=glm::scale(glm::mat4(1),glm::vec3(1/zoom));
  auto vp=glm::perspective(.8f,1.f,.1f,100.f)*view;
  auto eye=glm::inverse(vp)*glm::vec4(0,0,1,0);
  auto expected=glm::inverse(view)*glm::vec4(0,0,0,1);
  assert(std::abs(eye.y/eye.w-expected.y)<1e-4);
 }
}
'''
with tempfile.TemporaryDirectory() as tmp:
 cpp=Path(tmp)/'order.cpp';cpp.write_text(code);exe=Path(tmp)/'order'
 subprocess.run(['c++','-std=c++17','-I',str(root/'wf-v-kube/src'),str(cpp),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
print('Surface ordering: 3/4/6 faces, 54 camera/rotation cases, ray overlap order, split area conservation, and eye transforms passed')
