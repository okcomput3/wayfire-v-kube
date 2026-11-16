import ctypes
from OpenGL import GL
from OpenGL.GL.shaders import compileShader,compileProgram

def check_bloom(fragment):
    program=compileProgram(compileShader(fragment('accent-bloom.cpp','bloom_vertex'),GL.GL_VERTEX_SHADER),
        compileShader(fragment('accent-bloom.cpp','bloom_extract'),GL.GL_FRAGMENT_SHADER))
    GL.glUseProgram(program)
    GL.glDisable(GL.GL_BLEND);GL.glDisable(GL.GL_DEPTH_TEST)
    GL.glDisable(GL.GL_CULL_FACE);GL.glDisable(GL.GL_SCISSOR_TEST)
    data=(ctypes.c_float*8)(-1,-1,1,-1,-1,1,1,1)
    vbo=GL.glGenBuffers(1);GL.glBindBuffer(GL.GL_ARRAY_BUFFER,vbo)
    GL.glBufferData(GL.GL_ARRAY_BUFFER,ctypes.sizeof(data),data,GL.GL_STATIC_DRAW)
    loc=GL.glGetAttribLocation(program,'position');GL.glEnableVertexAttribArray(loc)
    GL.glVertexAttribPointer(loc,2,GL.GL_FLOAT,False,0,ctypes.c_void_p(0))
    textures=GL.glGenTextures(2)
    for i,name in enumerate(['original','accented']):
        GL.glActiveTexture(GL.GL_TEXTURE0+i);GL.glBindTexture(GL.GL_TEXTURE_2D,textures[i])
        GL.glTexParameteri(GL.GL_TEXTURE_2D,GL.GL_TEXTURE_MIN_FILTER,GL.GL_NEAREST)
        GL.glTexParameteri(GL.GL_TEXTURE_2D,GL.GL_TEXTURE_MAG_FILTER,GL.GL_NEAREST)
        GL.glUniform1i(GL.glGetUniformLocation(program,name),i)
    def draw(a,b,threshold):
        for i,color in enumerate([a,b]):
            GL.glActiveTexture(GL.GL_TEXTURE0+i);GL.glBindTexture(GL.GL_TEXTURE_2D,textures[i])
            GL.glTexImage2D(GL.GL_TEXTURE_2D,0,GL.GL_RGBA,1,1,0,GL.GL_RGBA,GL.GL_UNSIGNED_BYTE,bytes(color))
        GL.glUniform1f(GL.glGetUniformLocation(program,'threshold'),threshold)
        GL.glDrawArrays(GL.GL_TRIANGLE_STRIP,0,4)
        return tuple(GL.glReadPixels(32,32,1,1,GL.GL_RGBA,GL.GL_UNSIGNED_BYTE))
    assert draw([255]*4,[255]*4,0)==(0,0,0,0), 'White desktop leaked into bloom'
    assert draw([20,20,20,255],[220,100,20,255],.1)[0]>150
    assert draw([20,20,20,255],[40,30,20,255],.5)==(0,0,0,0), 'Threshold ignored'
    GL.glDisableVertexAttribArray(loc);GL.glDeleteBuffers(1,[vbo]);GL.glDeleteTextures(textures)
    GL.glDeleteProgram(program);GL.glActiveTexture(GL.GL_TEXTURE0)
    print('Selective bloom: bright desktop excluded, accent extracted, threshold respected')
