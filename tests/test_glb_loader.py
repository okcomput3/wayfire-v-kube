#!/usr/bin/env python3
"""Create a self-contained GLB and exercise production importer, including failures."""
import json, struct, subprocess, tempfile
from pathlib import Path
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as tmp:
    tmp=Path(tmp)
    positions=struct.pack('<9f',0,0,0, 2,0,0, 0,2,0)
    uv=struct.pack('<6f',0,0, 1,0, 0,1)
    image=(root/'examples/cube-checker.png').read_bytes()
    binary=positions+uv+image
    binary+=b'\0'*((-len(binary))%4)
    doc={'asset':{'version':'2.0'},'buffers':[{'byteLength':len(binary)}],
      'bufferViews':[{'buffer':0,'byteOffset':0,'byteLength':36}, {'buffer':0,'byteOffset':36,'byteLength':24},
                     {'buffer':0,'byteOffset':60,'byteLength':len(image)}],
      'accessors':[{'bufferView':0,'componentType':5126,'count':3,'type':'VEC3','min':[0,0,0],'max':[2,2,0]},
                   {'bufferView':1,'componentType':5126,'count':3,'type':'VEC2'}],
      'images':[{'bufferView':2,'mimeType':'image/png'}], 'textures':[{'source':0}],
      'materials':[{'pbrMetallicRoughness':{'baseColorFactor':[.2,.4,.6,1],'baseColorTexture':{'index':0}}}],
      'meshes':[{'primitives':[{'attributes':{'POSITION':0,'TEXCOORD_0':1},'material':0}]}],
      'nodes':[{'mesh':0,'translation':[10,20,30]}], 'scenes':[{'nodes':[0]}],'scene':0}
    def accessor(values, kind, count):
        # Append tightly packed float data to the fixture buffer.
        index = len(doc['accessors'])
        view = len(doc['bufferViews'])
        raw = struct.pack('<' + 'f'*len(values), *values)
        offset = len(binary_parts[0])
        binary_parts[0] += raw
        doc['bufferViews'].append({'buffer':0,'byteOffset':offset,'byteLength':len(raw)})
        doc['accessors'].append({'bufferView':view,'componentType':5126,'count':count,'type':kind})
        return index
    binary_parts = [binary]
    times = accessor([0,2], 'SCALAR', 2)
    linear = accessor([10,20,30,12,20,30], 'VEC3', 2)
    cubic = accessor([0,0,0,10,20,30,0,0,0,0,0,0,12,20,30,0,0,0], 'VEC3', 6)
    doc['animations'] = []
    for name, output in [('LINEAR',linear),('STEP',linear),('CUBICSPLINE',cubic)]:
        doc['animations'].append({'name':name,'samplers':[{'input':times,'output':output,'interpolation':name}],
            'channels':[{'sampler':0,'target':{'node':0,'path':'translation'}}]})
    binary = binary_parts[0]
    doc['buffers'][0]['byteLength'] = len(binary)
    def save(path):
        js=json.dumps(doc).encode();js+=b' '*((-len(js))%4)
        path.write_bytes(struct.pack('<III',0x46546c67,2,28+len(js)+len(binary))+
            struct.pack('<II',len(js),0x4e4f534a)+js+struct.pack('<II',len(binary),0x004e4942)+binary)
    good=tmp/'test.glb';save(good)
    doc['extensionsRequired']=['KHR_draco_mesh_compression']
    unsupported=tmp/'unsupported.glb';save(unsupported)
    bad=tmp/'broken.glb';bad.write_bytes(good.read_bytes()[:50])
    source=tmp/'test.cpp'
    source.write_text('''#include "glb-loader.hpp"
#include "glb-animation.hpp"
#include <cassert>
int main(int argc,char**argv) {
 cube_obj_mesh_t m; std::string e;
 assert(load_cube_glb(argv[1],m,e));
 assert(m.vertices.size()==3 && m.batches.size()==1 && m.materials.size()==2);
 assert(m.uv_top_left && m.images.size()==1);
 assert(m.vertices[0].position==glm::vec3(-.5f,-.5f,0));
 assert(m.vertices[1].uv==glm::vec2(1,0));
 assert(m.materials[1].diffuse==glm::vec3(.2f,.4f,.6f));
 assert(m.images.begin()->second.rgba.size()==64*64*4);
 assert(m.animation && m.animation->clips().size()==3);
 std::vector<cube_obj_vertex_t> pose;
 m.animation->sample(1,0,pose); assert(std::abs(pose[0].position.x)<1e-5f);
 m.animation->sample(1,1,pose); assert(std::abs(pose[0].position.x+.5f)<1e-5f);
 m.animation->sample(1,2,pose); assert(std::abs(pose[0].position.x)<1e-5f);
 m.animation->sample(3,0,pose); assert(std::abs(pose[0].position.x)<1e-5f);
 assert(!load_cube_glb(argv[2],m,e) && m.vertices.empty());
 assert(!load_cube_glb(argv[3],m,e) && m.vertices.empty());
}''')
    exe=tmp/'test'
    subprocess.run(['c++','-std=c++17','-O1','-I'+str(root/'wf-v-kube/src'),str(source),
        str(root/'wf-v-kube/src/glb-loader.cpp'), str(root/'wf-v-kube/src/glb-animation.cpp'),'-o',str(exe)],check=True)
    subprocess.run([str(exe),str(good),str(bad),str(unsupported)],check=True)
    # Also provide an original self-contained sample for interactive testing.
    # Fixture is temporary; do not rewrite the static example during tests.
print('GLB: scene transforms, embedded texture, UVs, materials, truncation and unsupported extensions passed')
