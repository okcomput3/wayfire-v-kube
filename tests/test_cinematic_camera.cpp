#include "cinematic-camera.hpp"
#include <cassert>
#include <cmath>
int main(){
 for(auto direction : {1,-1}) {
  for(int vertical : {0,1}) {
   auto x=vertical?0:direction,y=vertical?direction:0;
   for(float t : {-1.f,0.f,1.f,2.f}) {auto p=cube_camera_sweep(t,x,y);assert(p.yaw==0&&p.pitch==0&&p.roll==0&&p.pullback==0&&p.rise==0);}
   auto start=cube_camera_sweep(.0001f,x,y),end=cube_camera_sweep(.9999f,x,y);
   assert(std::abs(start.yaw)+std::abs(start.pitch)+std::abs(start.roll)<1e-6f);
   assert(std::abs(end.yaw)+std::abs(end.pitch)+std::abs(end.roll)<1e-6f);
   auto middle=cube_camera_sweep(.5f,x,y);assert(middle.pullback>.6f);
   assert(vertical ? middle.pitch*direction>.8f : middle.yaw*direction>1.f);
   for(int i=0;i<=1000;++i) {auto p=cube_camera_sweep(i/1000.f,x,y);assert(std::isfinite(p.yaw)&&std::abs(p.roll)<.3f&&p.pullback>=0);}
  }
 }
}
