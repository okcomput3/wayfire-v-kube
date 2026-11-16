"""Check actual OBJ material shaders with the caller's 64px EGL context."""
import ctypes
from OpenGL import GL
from OpenGL.GL.shaders import compileProgram, compileShader


def check_obj_materials(fragment):
    program = compileProgram(
        compileShader(fragment('obj-renderer.cpp', 'object_vertex_shader'), GL.GL_VERTEX_SHADER),
        compileShader(fragment('obj-renderer.cpp', 'object_fragment_shader'), GL.GL_FRAGMENT_SHADER))
    GL.glUseProgram(program)
    loc = lambda name: GL.glGetUniformLocation(program, name)
    identity = (ctypes.c_float * 16)(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1)
    for name in ('VP', 'model'):
        GL.glUniformMatrix4fv(loc(name), 1, False, identity)
    data = (ctypes.c_float * 32)(-1,-1,0, 0,0,1, 0,0,
        1,-1,0, 0,0,1, 1,0, -1,1,0, 0,0,1, 0,1, 1,1,0, 0,0,1, 1,1)
    vbo = GL.glGenBuffers(1)
    GL.glBindBuffer(GL.GL_ARRAY_BUFFER, vbo)
    GL.glBufferData(GL.GL_ARRAY_BUFFER, ctypes.sizeof(data), data, GL.GL_STATIC_DRAW)
    attributes = []
    for name, size, offset in [('position',3,0), ('normal',3,12), ('uv',2,24)]:
        i = GL.glGetAttribLocation(program, name)
        GL.glEnableVertexAttribArray(i)
        GL.glVertexAttribPointer(i, size, GL.GL_FLOAT, False, 32, ctypes.c_void_p(offset))
        attributes.append(i)
    textures = GL.glGenTextures(3)
    for i, name in enumerate(('diffuse', 'specular', 'opacity')):
        GL.glActiveTexture(GL.GL_TEXTURE0 + i)
        GL.glBindTexture(GL.GL_TEXTURE_2D, textures[i])
        GL.glTexParameteri(GL.GL_TEXTURE_2D, GL.GL_TEXTURE_MIN_FILTER, GL.GL_NEAREST)
        GL.glTexParameteri(GL.GL_TEXTURE_2D, GL.GL_TEXTURE_MAG_FILTER, GL.GL_NEAREST)
        GL.glTexImage2D(GL.GL_TEXTURE_2D, 0, GL.GL_RGBA, 1, 1, 0, GL.GL_RGBA, GL.GL_UNSIGNED_BYTE, bytes([255]*4))
        GL.glUniform1i(loc(name+'_texture'), i)
        GL.glUniform4f(loc(name+'_transform'), 1, 1, 0, 0)
    GL.glUniform3f(loc('map_clamp'), 1, 1, 1)
    GL.glUniform3f(loc('diffuse'), 1, 1, 1)
    GL.glUniform3f(loc('emission'), 0, 0, 0)
    GL.glUniform1i(loc('illumination'), 0)
    GL.glUniform1f(loc('opacity'), 1)
    GL.glActiveTexture(GL.GL_TEXTURE0)
    GL.glBindTexture(GL.GL_TEXTURE_2D, textures[0])
    # Image top row red, bottom row green; OBJ v=0 must sample green.
    GL.glTexImage2D(GL.GL_TEXTURE_2D, 0, GL.GL_RGBA, 1, 2, 0, GL.GL_RGBA, GL.GL_UNSIGNED_BYTE,
        bytes([255,0,0,255, 0,255,0,255]))
    GL.glDisable(GL.GL_BLEND)
    GL.glDisable(GL.GL_CULL_FACE)
    GL.glDisable(GL.GL_SCISSOR_TEST)
    GL.glEnable(GL.GL_DEPTH_TEST)
    GL.glDepthFunc(GL.GL_LESS)
    GL.glDepthMask(True)
    def draw():
        GL.glClearColor(0,0,1,1)
        GL.glClear(GL.GL_COLOR_BUFFER_BIT | GL.GL_DEPTH_BUFFER_BIT)
        GL.glDrawArrays(GL.GL_TRIANGLE_STRIP, 0, 4)
    def pixel(x,y):
        return bytes(GL.glReadPixels(x,y,1,1,GL.GL_RGBA,GL.GL_UNSIGNED_BYTE))[:3]
    draw()
    assert pixel(32,8) == bytes([0,255,0])
    assert pixel(32,56) == bytes([255,0,0])
    GL.glUniform1i(loc('uv_top_left'), 1)
    draw()
    assert pixel(32,8) == bytes([255,0,0]), 'GLB UV origin wrong'
    GL.glUniform1i(loc('uv_top_left'), 0)
    GL.glUniform3f(loc('diffuse'), .5, 1, 1)
    draw()
    assert abs(pixel(32,56)[0]-128) <= 1, 'Material tint ignored'
    GL.glUniform1f(loc('opacity'), 0)
    draw()
    assert pixel(32,32) == bytes([0,0,255]), 'Transparent material wrote color'
    # Draw the same depth again. It must pass if discarded pixels wrote no depth.
    GL.glUniform1f(loc('opacity'), 1)
    GL.glDrawArrays(GL.GL_TRIANGLE_STRIP, 0, 4)
    assert pixel(32,8) == bytes([0,255,0]), 'Transparent material wrote depth'
    GL.glUniform1f(loc('opacity'), .5)
    draw()
    pixels = bytes(GL.glReadPixels(0,0,64,64,GL.GL_RGBA,GL.GL_UNSIGNED_BYTE))
    holes = sum(pixels[i:i+3] == bytes([0,0,255]) for i in range(0,len(pixels),4))
    assert holes == 2048, ('Half opacity coverage', holes)
    # Explicit opacity map must also punch holes.
    GL.glUniform1f(loc('opacity'), 1)
    GL.glActiveTexture(GL.GL_TEXTURE2)
    GL.glBindTexture(GL.GL_TEXTURE_2D, textures[2])
    GL.glTexImage2D(GL.GL_TEXTURE_2D, 0, GL.GL_RGBA, 1,1,0,GL.GL_RGBA,GL.GL_UNSIGNED_BYTE,bytes([0,0,0,255]))
    draw()
    assert pixel(32,8) == bytes([0,0,255])
    for i in attributes: GL.glDisableVertexAttribArray(i)
    GL.glDeleteTextures(textures)
    GL.glDeleteBuffers(1, [vbo])
    GL.glDeleteProgram(program)
    GL.glDisable(GL.GL_DEPTH_TEST)
    GL.glActiveTexture(GL.GL_TEXTURE0)
    print('OBJ materials: UV orientation, color tint, opacity maps and transparent depth passed')
