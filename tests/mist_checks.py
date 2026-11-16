"""Render real mist shaders: front masking follows the camera, rear stays visible."""
import ctypes
from OpenGL import GL
from OpenGL.GL.shaders import compileShader, compileProgram


def check_mist(fragment):
    program = compileProgram(compileShader(fragment('cube-mist.cpp', 'mist_vertex'), GL.GL_VERTEX_SHADER),
        compileShader(fragment('cube-mist.cpp', 'mist_fragment'), GL.GL_FRAGMENT_SHADER))
    GL.glUseProgram(program)
    GL.glDisable(GL.GL_DEPTH_TEST)
    GL.glDisable(GL.GL_BLEND)
    GL.glDisable(GL.GL_CULL_FACE)
    GL.glDisable(GL.GL_SCISSOR_TEST)
    vertices = (ctypes.c_float * 30)(
        -1,-1,.5,-1,-1, 1,-1,.5,1,-1, 1,1,.5,1,1,
        -1,-1,.5,-1,-1, 1,1,.5,1,1, -1,1,.5,-1,1)
    vbo = GL.glGenBuffers(1)
    GL.glBindBuffer(GL.GL_ARRAY_BUFFER,vbo)
    GL.glBufferData(GL.GL_ARRAY_BUFFER,ctypes.sizeof(vertices),vertices,GL.GL_STATIC_DRAW)
    for name,size,offset in [('position',3,0),('texcoord',2,12)]:
        loc=GL.glGetAttribLocation(program,name)
        GL.glEnableVertexAttribArray(loc)
        GL.glVertexAttribPointer(loc,size,GL.GL_FLOAT,False,20,ctypes.c_void_p(offset))
    for name,value in [('seed',2.37),('row_y',0)]:
        loc=GL.glGetAttribLocation(program,name)
        GL.glDisableVertexAttribArray(loc)
        GL.glVertexAttrib1f(loc,value)
    matrix=(ctypes.c_float*16)(1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1)
    GL.glUniformMatrix4fv(GL.glGetUniformLocation(program,'VP'),1,False,matrix)
    GL.glUniform1f(GL.glGetUniformLocation(program,'opacity'),.7)
    def draw(camera,time=1):
        GL.glUniform3f(GL.glGetUniformLocation(program,'toward_camera'),*camera)
        GL.glUniform1f(GL.glGetUniformLocation(program,'time'),time)
        GL.glClearColor(0,0,0,0)
        GL.glClear(GL.GL_COLOR_BUFFER_BIT)
        GL.glDrawArrays(GL.GL_TRIANGLES,0,6)
        return bytes(GL.glReadPixels(0,0,64,64,GL.GL_RGBA,GL.GL_UNSIGNED_BYTE))
    assert not any(draw((0,0,1))), 'Front puff was not fully masked'
    rear=draw((0,0,-1))
    assert max(rear)>10, 'Rear puff missing'
    assert draw((0,0,-1),3)!=rear, 'Smoke turbulence frozen'
    tilted=draw((.707,0,.707))
    assert any(tilted), 'Side portion missing after camera rotation'
    assert not any(tilted[(32*64+48)*4:(32*64+49)*4]), 'Front portion leaked after rotation'
    for name in ['position','texcoord']:
        GL.glDisableVertexAttribArray(GL.glGetAttribLocation(program,name))
    GL.glDeleteBuffers(1,[vbo])
    GL.glDeleteProgram(program)
    print('Mist: front clear, rear visible, tilted camera mask and turbulence passed')
