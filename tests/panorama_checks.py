"""Compare actual background shader slices with one full panorama."""
import ctypes
from OpenGL import GL
from OpenGL.GL.shaders import compileProgram, compileShader


def check_panorama(fragment, alternative=False):
    program = compileProgram(
        compileShader(fragment('shader-background.cpp', 'background_vertex_shader'), GL.GL_VERTEX_SHADER),
        compileShader(fragment('alternative-background.tpp', 'alternative_fragment_shader') if alternative else fragment('shader-background.cpp', 'background_fragment_shader'), GL.GL_FRAGMENT_SHADER))
    GL.glUseProgram(program)
    GL.glDisable(GL.GL_DEPTH_TEST)
    GL.glDisable(GL.GL_BLEND)
    GL.glDisable(GL.GL_SCISSOR_TEST)
    vertices = (ctypes.c_float * 8)(-1, -1, 1, -1, -1, 1, 1, 1)
    vbo = GL.glGenBuffers(1)
    GL.glBindBuffer(GL.GL_ARRAY_BUFFER, vbo)
    GL.glBufferData(GL.GL_ARRAY_BUFFER, ctypes.sizeof(vertices), vertices, GL.GL_STATIC_DRAW)
    loc = GL.glGetAttribLocation(program, 'position')
    GL.glEnableVertexAttribArray(loc)
    GL.glVertexAttribPointer(loc, 2, GL.GL_FLOAT, False, 0, ctypes.c_void_p(0))
    def draw(x, width, origin, scale):
        GL.glViewport(x, 0, width, 32)
        GL.glUniform2f(GL.glGetUniformLocation(program, 'u_uv_origin'), origin, 0)
        GL.glUniform2f(GL.glGetUniformLocation(program, 'u_uv_scale'), scale, 1)
        GL.glDrawArrays(GL.GL_TRIANGLE_STRIP, 0, 4)
    for clock in (0.0, 1.25, 7.5):
        GL.glUniform1f(GL.glGetUniformLocation(program, 'u_time'), clock)
        GL.glUniform2f(GL.glGetUniformLocation(program, 'u_resolution'), 60, 32)
        draw(0, 60, 0, 1)
        full = bytes(GL.glReadPixels(0, 0, 60, 32, GL.GL_RGBA, GL.GL_UNSIGNED_BYTE))
        for column in range(3):
            draw(column * 20, 20, column / 3, 1 / 3)
        split = bytes(GL.glReadPixels(0, 0, 60, 32, GL.GL_RGBA, GL.GL_UNSIGNED_BYTE))
        error = sum(abs(a-b) for a, b in zip(full, split)) / len(full)
        assert error < 1, f'Panoramic background differs from a single image: {error}'
    GL.glDeleteBuffers(1, [vbo])
    GL.glDeleteProgram(program)
    GL.glViewport(0, 0, 64, 64)
    print('Panorama: three shader slices match one continuous background at three animation times')
