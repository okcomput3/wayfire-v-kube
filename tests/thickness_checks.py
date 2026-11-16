"""Actual GPU extrusion uses perimeter RGB, excludes interior/shadow pixels."""
import ctypes
from OpenGL import GL
from OpenGL.GL.shaders import compileProgram, compileShader

def check_thickness(fragment):
    program=compileProgram(*[compileShader(fragment('window-thickness.cpp',name),kind) for name,kind in [
        ('thickness_vertex',GL.GL_VERTEX_SHADER),('thickness_geometry',GL.GL_GEOMETRY_SHADER),
        ('thickness_fragment',GL.GL_FRAGMENT_SHADER)]])
    GL.glUseProgram(program)
    GL.glDisable(GL.GL_DEPTH_TEST); GL.glDisable(GL.GL_BLEND); GL.glDisable(GL.GL_CULL_FACE)
    identity=(ctypes.c_float*16)(1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1)
    view=(ctypes.c_float*16)(1,0,0,0,0,1,0,0,1,0,1,0,0,0,0,1)
    GL.glUniformMatrix4fv(GL.glGetUniformLocation(program,'VP'),1,False,view)
    GL.glUniformMatrix4fv(GL.glGetUniformLocation(program,'model'),1,False,identity)
    GL.glUniform2i(GL.glGetUniformLocation(program,'dimensions'),8,8)
    GL.glUniform2i(GL.glGetUniformLocation(program,'scan_origin'),0,0)
    GL.glUniform2i(GL.glGetUniformLocation(program,'scan_size'),8,8)
    for name,value in [('thickness',.3),('cutoff',.8),('opacity',1),('ease',1),('scale',1)]:
        GL.glUniform1f(GL.glGetUniformLocation(program,name),value)
    GL.glUniform1i(GL.glGetUniformLocation(program,'deform'),0)
    GL.glUniform1i(GL.glGetUniformLocation(program,'pixels'),0)
    tex=GL.glGenTextures(1); GL.glActiveTexture(GL.GL_TEXTURE0); GL.glBindTexture(GL.GL_TEXTURE_2D,tex)
    GL.glTexParameteri(GL.GL_TEXTURE_2D,GL.GL_TEXTURE_MIN_FILTER,GL.GL_NEAREST)
    GL.glTexParameteri(GL.GL_TEXTURE_2D,GL.GL_TEXTURE_MAG_FILTER,GL.GL_NEAREST)
    def draw(interior, edge=(220,70,30,255), shadow=(0,0,0,0), full=False, tiled=False):
        pixels=[]
        for y in range(8):
            for x in range(8):
                pixels.extend(edge if full else interior if 2<x<5 and 2<y<5 else edge if 2<=x<=5 and 2<=y<=5 else shadow)
        GL.glTexImage2D(GL.GL_TEXTURE_2D,0,GL.GL_RGBA,8,8,0,GL.GL_RGBA,GL.GL_UNSIGNED_BYTE,bytes(pixels))
        GL.glClearColor(0,0,0,0); GL.glClear(GL.GL_COLOR_BUFFER_BIT)
        if tiled:
            for x in (0,4):
                GL.glUniform2i(GL.glGetUniformLocation(program,'scan_origin'),x,0)
                GL.glUniform2i(GL.glGetUniformLocation(program,'scan_size'),4,8)
                GL.glDrawArrays(GL.GL_POINTS,0,32)
            GL.glUniform2i(GL.glGetUniformLocation(program,'scan_origin'),0,0)
            GL.glUniform2i(GL.glGetUniformLocation(program,'scan_size'),8,8)
        else:
            GL.glDrawArrays(GL.GL_POINTS,0,64)
        return bytes(GL.glReadPixels(0,0,64,64,GL.GL_RGBA,GL.GL_UNSIGNED_BYTE))
    base=draw((0,0,255,255))
    assert base==draw((0,0,255,255),tiled=True), 'Tile addressing changed the extrusion'
    assert any(base), 'No extruded side rendered'
    assert base==draw((0,255,0,255)), 'Interior colors leaked into edge extrusion'
    assert base==draw((0,255,0,255),shadow=(20,10,10,50)), 'Faint shadow became a slab edge'
    assert base!=draw((0,0,255,255),edge=(30,220,70,255)), 'Edge RGB not used'
    for i in range(0,len(base),4):
        if base[i+3]: assert tuple(base[i:i+4])==(220,70,30,255)
    GL.glUniform1f(GL.glGetUniformLocation(program,'thickness'),0)
    assert not any(draw((0,0,255,255))), 'Zero thickness emitted visible side walls'
    GL.glUniform1f(GL.glGetUniformLocation(program,'thickness'),.3)
    GL.glUniform1f(GL.glGetUniformLocation(program,'opacity'),0)
    assert not any(draw((0,0,255,255))), 'Zero opacity visible'
    GL.glUniform1f(GL.glGetUniformLocation(program,'opacity'),1)
    GL.glUniform1i(GL.glGetUniformLocation(program,'desktop'),0)
    assert not any(draw((0,0,0,255),full=True)), 'Window workspace boundary was extruded'
    GL.glUniform1i(GL.glGetUniformLocation(program,'desktop'),1)
    assert any(draw((0,0,0,255),full=True)), 'Desktop perimeter was not extruded'
    GL.glUniform1f(GL.glGetUniformLocation(program,'thickness'),0)
    assert not any(draw((0,0,0,255),full=True)), 'Disabled desktop thickness remained visible'
    print('Desktop thickness: opaque texture perimeter, separate window behavior, zero thickness passed')
    GL.glDeleteTextures([tex]); GL.glDeleteProgram(program)
    print('Window thickness: border RGB, interior exclusion, shadow cutoff and opacity passed')
