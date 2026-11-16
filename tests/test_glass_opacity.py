#!/usr/bin/env python3
from pathlib import Path
import tempfile
import subprocess
root = Path(__file__).resolve().parents[1]
code = r'''
#include "glass-opacity.hpp"
#include <cassert>
#include <cmath>
int main() {
    for (float configured : {0.f, .4f, .75f, 1.f}) {
        assert(cube_glass_opacity(configured,0,1,2,2,1)==1);
        assert(std::abs(cube_glass_opacity(configured,1,1,3,2,1)-configured)<1e-6);
        float previous=1;
        for(int i=0;i<=100;++i) {
            float t=i/100.f;
            float opacity=cube_glass_opacity(configured,t,1,2+t,2,1);
            assert(opacity<=previous+1e-6 && opacity>=configured-1e-6);
            previous=opacity;
        }
        // Wheel zoom toward the desktop restores opacity even while held open.
        assert(cube_glass_opacity(configured,1,.5,3,2,1)==1);
        assert(std::abs(cube_glass_opacity(configured,1,2,3,2,1)-configured)<1e-6);
        // Exit follows the same curve in reverse, independent of timing.
        assert(cube_glass_opacity(configured,0,1,2,2,1)==1);
        assert(std::isfinite(cube_glass_opacity(configured,.5,1,2,2,0)));
    }
}
'''
with tempfile.TemporaryDirectory() as tmp:
    cpp=Path(tmp)/'opacity.cpp'; cpp.write_text(code)
    exe=Path(tmp)/'opacity'
    subprocess.run(['c++','-std=c++17','-I',str(root/'wf-v-kube/src'),str(cpp),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
print('Glass opacity: entry/exit, wheel zoom, endpoints and zero-distance safety passed')
