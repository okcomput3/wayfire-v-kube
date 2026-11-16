#!/usr/bin/env python3
"""Render the actual cube fragment shaders with software EGL.

Requires Python PyOpenGL and Mesa EGL/GLES. No Wayfire session or GPU needed.
Checks transparent depth holes and premultiplied window blending in both paths.
"""
import os
os.environ.setdefault('PYOPENGL_PLATFORM', 'egl')
os.environ.setdefault('EGL_PLATFORM', 'surfaceless')
os.environ.setdefault('LIBGL_ALWAYS_SOFTWARE', '1')

import argparse
import ctypes
from pathlib import Path
import re
from OpenGL import EGL, GL
from OpenGL.GL.shaders import compileShader, compileProgram

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source-dir', type=Path,
    default=Path(__file__).resolve().parents[1] / 'wf-v-kube' / 'src')
root = parser.parse_args().source_dir


def fragment(filename, name):
    source = (root / filename).read_text()
    source = re.sub(r'/\*.*?\*/', '', source, flags=re.S)
    return re.search(name + r'\s*=\s*R"\((.*?)\)"', source, re.S)[1]


def pixel(x):
    return tuple(GL.glReadPixels(x, 32, 1, 1, GL.GL_RGBA, GL.GL_UNSIGNED_BYTE))


def near(actual, expected):
    assert all(abs(a - b) <= 2 for a, b in zip(actual[:3], expected)), (actual, expected)


def check(version, frag):
    varying = 'varying highp vec2 uvpos;' if version == 100 else 'out highp vec2 guv; out highp vec3 colorFactor;'
    varying += ' varying highp vec3 lightingPosition;' if version == 100 else ' out highp vec3 lightingPosition;'
    assignments = 'uvpos = uv;' if version == 100 else 'guv = uv; colorFactor = vec3(1.0);'
    assignments += ' lightingPosition = vec3(pos, 0.5);'
    attribute = 'attribute' if version == 100 else 'in'
    vert = f'''#version {version}{' es' if version > 100 else ''}
    {attribute} highp vec2 pos;
    {attribute} highp vec2 uv;
    uniform highp float depth;
    {varying}
    void main() {{ gl_Position = vec4(pos, depth, 1.0); {assignments} }}
    '''
    program = compileProgram(compileShader(vert, GL.GL_VERTEX_SHADER),
        compileShader(frag, GL.GL_FRAGMENT_SHADER))
    GL.glUseProgram(program)
    vertices = (ctypes.c_float * 24)(
        -1, -1, 0, 0, 1, -1, 1, 0, 1, 1, 1, 1,
        -1, -1, 0, 0, 1, 1, 1, 1, -1, 1, 0, 1)
    vbo = GL.glGenBuffers(1)
    GL.glBindBuffer(GL.GL_ARRAY_BUFFER, vbo)
    GL.glBufferData(GL.GL_ARRAY_BUFFER, ctypes.sizeof(vertices), vertices, GL.GL_STATIC_DRAW)
    for name, offset in [('pos', 0), ('uv', 8)]:
        loc = GL.glGetAttribLocation(program, name)
        GL.glEnableVertexAttribArray(loc)
        GL.glVertexAttribPointer(loc, 2, GL.GL_FLOAT, False, 16, ctypes.c_void_p(offset))
    tex = GL.glGenTextures(1)
    GL.glBindTexture(GL.GL_TEXTURE_2D, tex)
    GL.glTexParameteri(GL.GL_TEXTURE_2D, GL.GL_TEXTURE_MIN_FILTER, GL.GL_NEAREST)
    GL.glTexParameteri(GL.GL_TEXTURE_2D, GL.GL_TEXTURE_MAG_FILTER, GL.GL_NEAREST)
    GL.glUniform1i(GL.glGetUniformLocation(program, 'smp'), 0)
    GL.glUniform1f(GL.glGetUniformLocation(program, 'surface_opacity'), 1.0)
    GL.glEnable(GL.GL_DEPTH_TEST)
    GL.glDepthFunc(GL.GL_LESS)
    GL.glEnable(GL.GL_BLEND)
    GL.glBlendFunc(GL.GL_ONE, GL.GL_ONE_MINUS_SRC_ALPHA)

    def draw(depth, colors):
        GL.glTexImage2D(GL.GL_TEXTURE_2D, 0, GL.GL_RGBA, 2, 1, 0,
            GL.GL_RGBA, GL.GL_UNSIGNED_BYTE, bytes(colors))
        GL.glUniform1f(GL.glGetUniformLocation(program, 'depth'), depth)
        GL.glDrawArrays(GL.GL_TRIANGLES, 0, 6)

    GL.glClearColor(0, 0, 0, 1)
    GL.glClear(GL.GL_COLOR_BUFFER_BIT | GL.GL_DEPTH_BUFFER_BIT)
    # A nearer window covers only the right half. Its left half must not write depth.
    draw(-0.5, [0, 0, 0, 0, 0, 0, 255, 255])
    draw(0.5, [255, 0, 0, 255] * 2)
    near(pixel(16), (255, 0, 0))
    near(pixel(48), (0, 0, 255))

    GL.glClear(GL.GL_COLOR_BUFFER_BIT | GL.GL_DEPTH_BUFFER_BIT)
    draw(0.5, [255, 0, 0, 255] * 2)
    draw(-0.5, [0, 0, 128, 128] * 2)
    near(pixel(32), (127, 0, 128))
    # Build each layer's opaque color/depth together before the glass pass
    # samples the backdrop. Future front windows must not punch unpainted
    # background holes in a rear desktop. Reverse winding tests both sides.
    GL.glDisable(GL.GL_CULL_FACE)
    GL.glFrontFace(GL.GL_CW)
    GL.glDepthMask(True)
    GL.glClear(GL.GL_COLOR_BUFFER_BIT | GL.GL_DEPTH_BUFFER_BIT)
    GL.glDepthFunc(GL.GL_LEQUAL)

    def layer(depth, colors, opacity=1):
        GL.glUniform1f(GL.glGetUniformLocation(program, 'surface_opacity'), opacity)
        GL.glUniform1i(GL.glGetUniformLocation(program, 'depth_only'), 1)
        GL.glUniform1i(GL.glGetUniformLocation(program, 'translucent_only'), 0)
        GL.glDepthMask(True)
        GL.glDisable(GL.GL_BLEND)
        draw(depth, colors)
        GL.glUniform1i(GL.glGetUniformLocation(program, 'depth_only'), 0)
        GL.glUniform1i(GL.glGetUniformLocation(program, 'translucent_only'), 1)
        GL.glDepthMask(False)
        GL.glEnable(GL.GL_BLEND)
        draw(depth, colors)

    layer(.5, [255, 0, 0, 255] * 2)
    # Both points must contain the rear desktop when native blur reads them,
    # even where the later foreground window will eventually cover it.
    near(pixel(16), (255, 0, 0))
    near(pixel(48), (255, 0, 0))
    layer(-.3, [0, 255, 0, 255] * 2, .4)
    layer(-.7, [0, 0, 0, 0, 0, 0, 255, 255])
    near(pixel(16), (153, 102, 0))
    near(pixel(48), (0, 0, 255))
    # Mixed opaque/translucent content must be drawn once, never twice.
    GL.glDepthMask(True)
    GL.glClear(GL.GL_COLOR_BUFFER_BIT | GL.GL_DEPTH_BUFFER_BIT)
    layer(.5, [255, 0, 0, 255] * 2)
    layer(-.5, [0, 0, 128, 128, 0, 0, 255, 255])
    near(pixel(16), (127, 0, 128))
    near(pixel(48), (0, 0, 255))
    GL.glUniform1i(GL.glGetUniformLocation(program, 'translucent_only'), 0)
    GL.glDepthMask(True)
    GL.glDepthFunc(GL.GL_LESS)
    GL.glFrontFace(GL.GL_CCW)
    print(f'GLES shader {version}: complete blur backdrop and single-blend layers passed')
    assert GL.glGetError() == GL.GL_NO_ERROR
    GL.glDisable(GL.GL_DEPTH_TEST)
    GL.glDisable(GL.GL_BLEND)
    GL.glUniform1f(GL.glGetUniformLocation(program, 'surface_opacity'), 1)
    GL.glUniform1i(GL.glGetUniformLocation(program, 'dynamic_lighting'), 1)
    GL.glUniform1f(GL.glGetUniformLocation(program, 'lighting_strength'), .7)
    GL.glUniform1f(GL.glGetUniformLocation(program, 'lighting_radius'), .5)
    GL.glUniform1f(GL.glGetUniformLocation(program, 'lighting_row'), 0)
    GL.glUniform1f(GL.glGetUniformLocation(program, 'lighting_time'), 0)
    draw(0, [0, 0, 0, 128] * 2)
    first = pixel(32)
    GL.glUniform1f(GL.glGetUniformLocation(program, 'lighting_time'), 2)
    draw(0, [0, 0, 0, 128] * 2)
    later = pixel(32)
    assert first[:3] != later[:3] and max(first[:3]) > 0
    assert first[3] == later[3] == 128
    assert max(first[:3]) <= 128 and max(later[:3]) <= 128
    GL.glUniform1i(GL.glGetUniformLocation(program, 'dynamic_lighting'), 0)
    draw(0, [0, 0, 0, 128] * 2)
    assert pixel(32) == (0,0,0,128)
    print(f'GLES shader {version}: moving colored lights, unchanged alpha, and off switch passed')
    if version == 320:
        GL.glUniform1i(GL.glGetUniformLocation(program, 'window_layer'), 0)
        GL.glClearColor(0, 0, 0, 0)
        GL.glClear(GL.GL_COLOR_BUFFER_BIT)
        draw(0, [0, 255, 0, 255] * 2)
        desktop_edge = pixel(0)[3]
        assert desktop_edge < 255
        # Both edges of a split window must keep their original opacity.
        GL.glUniform1i(GL.glGetUniformLocation(program, 'window_layer'), 1)
        draw(0, [0, 255, 0, 255] * 2)
        assert pixel(0) == pixel(63) == (0, 255, 0, 255)
        assert tuple(GL.glReadPixels(0,0,1,1,GL.GL_RGBA,GL.GL_UNSIGNED_BYTE)) == (0,255,0,255)
        # Genuine translucent window content still keeps its native alpha.
        draw(0, [0, 128, 0, 128] * 2)
        assert pixel(0) == pixel(63) == (0,128,0,128)
        GL.glUniform1i(GL.glGetUniformLocation(program, 'window_layer'), 0)
        print('Window seam: solid and translucent edges preserved; desktop rounding retained')
    GL.glDeleteTextures([tex])
    GL.glDeleteBuffers(1, [vbo])
    GL.glDeleteProgram(program)
    print(f'GLES shader {version}: transparent depth and premultiplied blending passed')


def check_popout_blur():
    # Test the actual extraction shaders with representative native-blur output.
    # This checks the mask/composite step, not the installed blur algorithm.
    vert = fragment('popout-compositor.cpp', 'resolve_vertex')
    coverage = compileProgram(compileShader(vert, GL.GL_VERTEX_SHADER),
        compileShader(fragment('popout-compositor.cpp', 'coverage_fragment'), GL.GL_FRAGMENT_SHADER))
    resolve = compileProgram(compileShader(vert, GL.GL_VERTEX_SHADER),
        compileShader(fragment('popout-compositor.cpp', 'resolve_fragment'), GL.GL_FRAGMENT_SHADER))
    vbo = GL.glGenBuffers(1)
    vertices = (ctypes.c_float * 8)(-1, -1, 1, -1, -1, 1, 1, 1)
    GL.glBindBuffer(GL.GL_ARRAY_BUFFER, vbo)
    GL.glBufferData(GL.GL_ARRAY_BUFFER, ctypes.sizeof(vertices), vertices, GL.GL_STATIC_DRAW)
    GL.glDisable(GL.GL_DEPTH_TEST)
    GL.glDisable(GL.GL_BLEND)

    def use(program):
        GL.glUseProgram(program)
        loc = GL.glGetAttribLocation(program, 'position')
        GL.glEnableVertexAttribArray(loc)
        GL.glVertexAttribPointer(loc, 2, GL.GL_FLOAT, False, 0, ctypes.c_void_p(0))

    def texture(unit, rgba):
        GL.glActiveTexture(GL.GL_TEXTURE0 + unit)
        tex = GL.glGenTextures(1)
        GL.glBindTexture(GL.GL_TEXTURE_2D, tex)
        GL.glTexParameteri(GL.GL_TEXTURE_2D, GL.GL_TEXTURE_MIN_FILTER, GL.GL_NEAREST)
        GL.glTexParameteri(GL.GL_TEXTURE_2D, GL.GL_TEXTURE_MAG_FILTER, GL.GL_NEAREST)
        GL.glTexImage2D(GL.GL_TEXTURE_2D, 0, GL.GL_RGBA, 1, 1, 0,
            GL.GL_RGBA, GL.GL_UNSIGNED_BYTE, bytes(rgba))
        return tex

    for a, blurred, threshold, exponent in [
            (0, True, .5, 1), (51, True, .5, 1),
            (128, True, .5, 1), (255, True, .5, 1),
            (51, False, .5, 1), (51, True, .7, 2)]:
        use(coverage)
        tex = texture(0, [0, 0, 0, a])
        GL.glUniform1i(GL.glGetUniformLocation(coverage, 'window_tex'), 0)
        for name, value in [('blurred', float(blurred)), ('alpha_threshold', threshold),
                ('alpha_exponent', exponent)]:
            GL.glUniform1f(GL.glGetUniformLocation(coverage, name), value)
        GL.glClear(GL.GL_COLOR_BUFFER_BIT)
        GL.glDrawArrays(GL.GL_TRIANGLE_STRIP, 0, 4)
        mask = pixel(32)[3]
        opacity = a / 255
        strength = min(opacity / threshold, 1) ** exponent if blurred else 0
        expected = opacity + (1 - opacity) * strength
        assert abs(mask / 255 - expected) < .01, (mask, expected)
        GL.glDeleteTextures([tex])

        # Native result: blue translucent window, green blurred wallpaper,
        # and residual unblurred red wallpaper at low blur coverage.
        native = [(1 - opacity) * (1 - strength),
                  (1 - opacity) * strength, opacity]
        use(resolve)
        textures = [texture(0, [round(x * 255) for x in native] + [255]),
                    texture(1, [mask] * 4), texture(2, [255, 0, 0, 255])]
        for unit, name in enumerate(['composite_tex', 'coverage_tex', 'backdrop_tex']):
            GL.glUniform1i(GL.glGetUniformLocation(resolve, name), unit)
        GL.glDrawArrays(GL.GL_TRIANGLE_STRIP, 0, 4)
        actual = pixel(32)
        near(actual, (0, round(native[1] * 255), a))
        assert abs(actual[3] - mask) <= 1, (actual, mask)
        GL.glDeleteTextures(textures)

    # Coverage union keeps mixed blurred and ordinary windows separate.
    use(coverage)
    GL.glEnable(GL.GL_BLEND)
    GL.glBlendFunc(GL.GL_ONE, GL.GL_ONE_MINUS_SRC_ALPHA)
    GL.glClearColor(0, 0, 0, 0)
    GL.glClear(GL.GL_COLOR_BUFFER_BIT)
    remaining = 1.0
    for a, blurred in [(51, True), (102, False)]:
        tex = texture(0, [0, 0, 0, a])
        for name, value in [('blurred', float(blurred)), ('alpha_threshold', .5), ('alpha_exponent', 1)]:
            GL.glUniform1f(GL.glGetUniformLocation(coverage, name), value)
        GL.glDrawArrays(GL.GL_TRIANGLE_STRIP, 0, 4)
        opacity = a / 255
        strength = min(opacity / .5, 1) if blurred else 0
        remaining *= (1 - opacity) * (1 - strength)
        GL.glDeleteTextures([tex])
    assert abs(pixel(32)[3] / 255 - (1 - remaining)) < .01
    # The scratch buffer can contain opaque data outside this window. The
    # optimized coverage pass must touch only the window's scissored bounds.
    GL.glDisable(GL.GL_BLEND)
    GL.glClearColor(0, 0, 0, 0)
    GL.glClear(GL.GL_COLOR_BUFFER_BIT)
    tex = texture(0, [255, 255, 255, 255])
    GL.glEnable(GL.GL_SCISSOR_TEST)
    GL.glScissor(24, 24, 16, 16)
    GL.glDrawArrays(GL.GL_TRIANGLE_STRIP, 0, 4)
    GL.glDisable(GL.GL_SCISSOR_TEST)
    assert pixel(32)[3] == 255
    assert pixel(16)[3] == 0
    assert pixel(48)[3] == 0
    GL.glDeleteTextures([tex])
    print('Scissored coverage: window retained, stale scratch pixels excluded')

    # Compare full-screen extraction with the optimized region union, including
    # disjoint windows, old positions after a move, and a fully empty face.
    use(resolve)
    textures = [texture(0, [0, 128, 128, 255]),
                texture(1, [0, 0, 0, 0]), texture(2, [255, 0, 0, 255])]
    for unit, name in enumerate(['composite_tex', 'coverage_tex', 'backdrop_tex']):
        GL.glUniform1i(GL.glGetUniformLocation(resolve, name), unit)
    for rects in [[(4, 8, 16, 40), (40, 16, 20, 32)], [(24, 24, 12, 16)], []]:
        mask = bytearray(64 * 64 * 4)
        for x, y, w, h in rects:
            for row in range(y, y + h):
                for col in range(x, x + w):
                    offset = (row * 64 + col) * 4
                    mask[offset:offset + 4] = bytes([255] * 4)
        GL.glActiveTexture(GL.GL_TEXTURE1)
        GL.glBindTexture(GL.GL_TEXTURE_2D, textures[1])
        GL.glTexImage2D(GL.GL_TEXTURE_2D, 0, GL.GL_RGBA, 64, 64, 0,
            GL.GL_RGBA, GL.GL_UNSIGNED_BYTE, bytes(mask))
        GL.glDrawArrays(GL.GL_TRIANGLE_STRIP, 0, 4)
        expected = bytes(GL.glReadPixels(0, 0, 64, 64, GL.GL_RGBA, GL.GL_UNSIGNED_BYTE))
        GL.glClearColor(1, 0, 1, 1)
        GL.glClear(GL.GL_COLOR_BUFFER_BIT)
        GL.glClearColor(0, 0, 0, 0)
        GL.glClear(GL.GL_COLOR_BUFFER_BIT)
        GL.glEnable(GL.GL_SCISSOR_TEST)
        for rect in rects:
            GL.glScissor(*rect)
            GL.glDrawArrays(GL.GL_TRIANGLE_STRIP, 0, 4)
        GL.glDisable(GL.GL_SCISSOR_TEST)
        actual = bytes(GL.glReadPixels(0, 0, 64, 64, GL.GL_RGBA, GL.GL_UNSIGNED_BYTE))
        assert actual == expected, 'Region extraction differs from full-screen extraction'
    GL.glDeleteTextures(textures)
    print('Region extraction: disjoint, moved, and empty windows match full-screen output')
    GL.glDisable(GL.GL_BLEND)
    GL.glActiveTexture(GL.GL_TEXTURE0)
    GL.glDeleteProgram(coverage)
    GL.glDeleteProgram(resolve)
    GL.glDeleteBuffers(1, [vbo])
    print('Popout blur: transparent cutout, native coverage settings, and mixed-window masks passed')


display = EGL.eglGetDisplay(EGL.EGL_DEFAULT_DISPLAY)
major, minor = EGL.EGLint(), EGL.EGLint()
assert EGL.eglInitialize(display, major, minor)
EGL.eglBindAPI(EGL.EGL_OPENGL_ES_API)
attrs = (EGL.EGLint * 15)(EGL.EGL_SURFACE_TYPE, EGL.EGL_PBUFFER_BIT,
    EGL.EGL_RENDERABLE_TYPE, EGL.EGL_OPENGL_ES3_BIT,
    EGL.EGL_RED_SIZE, 8, EGL.EGL_GREEN_SIZE, 8, EGL.EGL_BLUE_SIZE, 8,
    EGL.EGL_ALPHA_SIZE, 8, EGL.EGL_DEPTH_SIZE, 24, EGL.EGL_NONE)
configs, count = (EGL.EGLConfig * 1)(), EGL.EGLint()
assert EGL.eglChooseConfig(display, attrs, configs, 1, count) and count.value
surface = EGL.eglCreatePbufferSurface(display, configs[0],
    (EGL.EGLint * 5)(EGL.EGL_WIDTH, 64, EGL.EGL_HEIGHT, 64, EGL.EGL_NONE))
context = EGL.eglCreateContext(display, configs[0], EGL.EGL_NO_CONTEXT,
    (EGL.EGLint * 3)(EGL.EGL_CONTEXT_CLIENT_VERSION, 3, EGL.EGL_NONE))
assert EGL.eglMakeCurrent(display, surface, surface, context)
try:
    GL.glViewport(0, 0, 64, 64)
    check(100, fragment('shaders.tpp', 'cube_fragment_2_0'))
    check(320, fragment('shaders-3-2.tpp', 'cube_fragment_3_2'))
    check_popout_blur()
    from obj_material_checks import check_obj_materials
    check_obj_materials(fragment)
    from deformation_checks import check_deformation
    check_deformation(fragment)
    from panorama_checks import check_panorama
    check_panorama(fragment)
    check_panorama(fragment, alternative=True)
    from mist_checks import check_mist
    check_mist(fragment)
    from thickness_checks import check_thickness
    check_thickness(fragment)
    from bloom_checks import check_bloom
    check_bloom(fragment)
finally:
    EGL.eglMakeCurrent(display, EGL.EGL_NO_SURFACE, EGL.EGL_NO_SURFACE, EGL.EGL_NO_CONTEXT)
    EGL.eglDestroyContext(display, context)
    EGL.eglDestroySurface(display, surface)
    EGL.eglTerminate(display)
