"""GPU transform-feedback checks for the actual deformation and cap shaders."""
import ctypes
import math
import struct
from OpenGL import GL
from OpenGL.GL.shaders import compileShader, compileProgram


def check_deformation(fragment):
    def program(stages, varying):
        result = GL.glCreateProgram()
        shaders = [compileShader(source, kind) for kind, source in stages]
        for shader in shaders:
            GL.glAttachShader(result, shader)
        name = ctypes.create_string_buffer(varying.encode())
        names = (ctypes.POINTER(ctypes.c_char) * 1)(ctypes.cast(name, ctypes.POINTER(ctypes.c_char)))
        GL.glTransformFeedbackVaryings(result, 1, names, GL.GL_INTERLEAVED_ATTRIBS)
        GL.glLinkProgram(result)
        assert GL.glGetProgramiv(result, GL.GL_LINK_STATUS), GL.glGetProgramInfoLog(result)
        for shader in shaders:
            GL.glDeleteShader(shader)
        return result

    side_stages = [(kind, fragment('shaders-3-2.tpp', name)) for kind, name in [
        (GL.GL_VERTEX_SHADER, 'cube_vertex_3_2'),
        (GL.GL_TESS_CONTROL_SHADER, 'cube_tcs_3_2'),
        (GL.GL_TESS_EVALUATION_SHADER, 'cube_tes_3_2')]]
    side = program(side_stages + [(GL.GL_FRAGMENT_SHADER, '''#version 320 es
precision highp float;
out vec4 color;
void main() { color = vec4(1); }''')], 'tePosition')
    # Also link the full production pipeline, including geometry and fragment.
    complete = compileProgram(*[compileShader(source, kind) for kind, source in side_stages + [
        (GL.GL_GEOMETRY_SHADER, fragment('shaders-3-2.tpp', 'cube_geometry_3_2')),
        (GL.GL_FRAGMENT_SHADER, fragment('shaders-3-2.tpp', 'cube_fragment_3_2'))]])
    cap = program([(GL.GL_VERTEX_SHADER, fragment('cube.cpp', 'cube_cap_vertex')),
                   (GL.GL_FRAGMENT_SHADER, fragment('cube.cpp', 'cube_cap_fragment'))], 'worldPos')
    vbo, feedback = GL.glGenBuffers(2)
    GL.glBindBuffer(GL.GL_TRANSFORM_FEEDBACK_BUFFER, feedback)
    GL.glBufferData(GL.GL_TRANSFORM_FEEDBACK_BUFFER, 2 ** 20, None, GL.GL_STREAM_READ)
    GL.glBindBufferBase(GL.GL_TRANSFORM_FEEDBACK_BUFFER, 0, feedback)
    identity = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]

    def capture(prog, vertices, model, deform, ease, scale=1, light=0):
        GL.glUseProgram(prog)
        for name, value in [('deform', deform), ('light', light)]:
            GL.glUniform1i(GL.glGetUniformLocation(prog, name), value)
        for name, value in [('ease', ease), ('popout_scale', scale)]:
            GL.glUniform1f(GL.glGetUniformLocation(prog, name), value)
        for name, matrix in [('VP', identity), ('model', model)]:
            GL.glUniformMatrix4fv(GL.glGetUniformLocation(prog, name), 1, False,
                (ctypes.c_float * 16)(*matrix))
        data = (ctypes.c_float * len(vertices))(*vertices)
        GL.glBindBuffer(GL.GL_ARRAY_BUFFER, vbo)
        GL.glBufferData(GL.GL_ARRAY_BUFFER, ctypes.sizeof(data), data, GL.GL_STATIC_DRAW)
        loc = GL.glGetAttribLocation(prog, 'position')
        GL.glEnableVertexAttribArray(loc)
        GL.glVertexAttribPointer(loc, 2, GL.GL_FLOAT, False, 0, ctypes.c_void_p(0))
        uv = GL.glGetAttribLocation(prog, 'uvPosition')
        if uv >= 0:
            GL.glDisableVertexAttribArray(uv)
            GL.glVertexAttrib2f(uv, .5, .5)
        query = GL.glGenQueries(1)
        query = int(query[0]) if hasattr(query, '__len__') else int(query)
        GL.glEnable(GL.GL_RASTERIZER_DISCARD)
        GL.glBeginQuery(GL.GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN, query)
        GL.glBeginTransformFeedback(GL.GL_TRIANGLES if prog == side else GL.GL_POINTS)
        if prog == side:
            GL.glPatchParameteri(GL.GL_PATCH_VERTICES, 3)
        GL.glDrawArrays(GL.GL_PATCHES if prog == side else GL.GL_POINTS, 0, len(vertices) // 2)
        GL.glEndTransformFeedback()
        GL.glEndQuery(GL.GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN)
        GL.glDisable(GL.GL_RASTERIZER_DISCARD)
        count = int(GL.glGetQueryObjectuiv(query, GL.GL_QUERY_RESULT)) * (3 if prog == side else 1)
        GL.glBindBuffer(GL.GL_TRANSFORM_FEEDBACK_BUFFER, feedback)
        ptr = GL.glMapBufferRange(GL.GL_TRANSFORM_FEEDBACK_BUFFER, 0, count * 12, GL.GL_MAP_READ_BIT)
        values = struct.unpack(f'{count * 3}f', ctypes.string_at(ptr, count * 12))
        GL.glUnmapBuffer(GL.GL_TRANSFORM_FEEDBACK_BUFFER)
        GL.glDeleteQueries(1, [query])
        return list(zip(values[::3], values[1::3], values[2::3]))

    for faces in [3, 4, 6]:
        z = .5 / math.tan(math.pi / faces)
        for mode in [0, 1, 2]:
            for ease in [0, .5, 1]:
                base_model = identity.copy()
                base_model[14] = z
                base = capture(side, [-.5, -.5, .5, -.5, .5, .5], base_model, mode, ease)
                scale = 1.2
                pop_model = base_model.copy()
                pop_model[0] = pop_model[5] = pop_model[10] = scale
                pop_model[14] = z * scale
                popped = capture(side, [-.5, -.5, .5, -.5, .5, .5], pop_model, mode, ease, scale)
                assert len(base) == len(popped)
                assert max(abs(b * scale - p) for bv, pv in zip(base, popped)
                           for b, p in zip(bv, pv)) < 1e-5, (faces, mode, ease)
                for light in [0, 1]:
                    segments = (50 if light else 30) if mode else 1
                    vertices = [coordinate for step in range(segments + 1)
                                for coordinate in [-.5 + step / segments, z]]
                    cap_model = identity.copy()
                    cap_model[13] = -.5
                    edge = capture(cap, vertices, cap_model, mode, ease)
                    side_edge = capture(side, [-.5, -.5, .5, -.5, .5, .5], base_model,
                                        mode, ease, light=light)
                    # Every cap boundary sample must lie on a side tessellation vertex.
                    assert max(min(math.dist(c, s) for s in side_edge) for c in edge) < .002, (faces, mode, ease, light)
    GL.glUseProgram(cap)
    GL.glUniform1i(GL.glGetUniformLocation(cap, 'cap_bump_3d'), 1)
    GL.glUniform1f(GL.glGetUniformLocation(cap, 'cap_height'), .025)
    GL.glUniform1f(GL.glGetUniformLocation(cap, 'cap_apothem'), .5)
    GL.glUniform1f(GL.glGetUniformLocation(cap, 'u_num_sides'), 4)
    GL.glUniform1f(GL.glGetUniformLocation(cap, 'time'), 0)
    raised = capture(cap, [0,0, 0,.5], identity, 0, 1)
    assert .01 < raised[0][1] < .015, raised
    assert abs(raised[1][1]) < 1e-6, '3D ripples opened a gap at cap edge'
    flat = capture(cap, [0,0], identity, 0, 0)
    assert abs(flat[0][1]) < 1e-6, '3D ripples did not flatten on desktop'
    GL.glUniform1i(GL.glGetUniformLocation(cap, 'cap_bump_3d'), 0)
    print('3D cap ripple geometry: displaced interior, fixed perimeter, flat desktop endpoint')
    # Draw the real cap shader over a rear red desktop, then compare against
    # its isolated premultiplied output. The translucent depth prepass must
    # leave the rear desktop visible, and cap alpha zero must leave no color.
    GL.glUseProgram(cap)
    GL.glDisable(GL.GL_CULL_FACE)
    GL.glDisable(GL.GL_DEPTH_TEST)
    GL.glDisable(GL.GL_BLEND)
    GL.glUniform1i(GL.glGetUniformLocation(cap, 'deform'), 0)
    GL.glUniform1i(GL.glGetUniformLocation(cap, 'depth_only'), 0)
    GL.glUniform1f(GL.glGetUniformLocation(cap, 'cap_alpha'), .4)
    GL.glUniform1f(GL.glGetUniformLocation(cap, 'time'), 0)
    # Project the cap's X/Z plane onto the screen.
    projection = [1,0,0,0, 0,0,1,0, 0,1,0,0, 0,0,0,1]
    GL.glUniformMatrix4fv(GL.glGetUniformLocation(cap, 'VP'), 1, False, (ctypes.c_float*16)(*projection))
    GL.glUniformMatrix4fv(GL.glGetUniformLocation(cap, 'model'), 1, False, (ctypes.c_float*16)(*identity))
    vertices = (ctypes.c_float*8)(-1,-1, 1,-1, -1,1, 1,1)
    GL.glBindBuffer(GL.GL_ARRAY_BUFFER, vbo)
    GL.glBufferData(GL.GL_ARRAY_BUFFER, ctypes.sizeof(vertices), vertices, GL.GL_STATIC_DRAW)
    loc=GL.glGetAttribLocation(cap, 'position')
    GL.glEnableVertexAttribArray(loc)
    GL.glVertexAttribPointer(loc, 2, GL.GL_FLOAT, False, 0, ctypes.c_void_p(0))
    uv=GL.glGetAttribLocation(cap, 'uvPosition')
    GL.glDisableVertexAttribArray(uv)
    GL.glVertexAttrib2f(uv, .5, .5)
    tex=GL.glGenTextures(1)
    GL.glActiveTexture(GL.GL_TEXTURE0)
    GL.glBindTexture(GL.GL_TEXTURE_2D, tex)
    GL.glTexParameteri(GL.GL_TEXTURE_2D, GL.GL_TEXTURE_MIN_FILTER, GL.GL_NEAREST)
    GL.glTexParameteri(GL.GL_TEXTURE_2D, GL.GL_TEXTURE_MAG_FILTER, GL.GL_NEAREST)
    GL.glTexImage2D(GL.GL_TEXTURE_2D, 0, GL.GL_RGBA, 1, 1, 0, GL.GL_RGBA, GL.GL_UNSIGNED_BYTE, bytes([0,255,0,255]))
    GL.glUniform1i(GL.glGetUniformLocation(cap, 'cap_picture'), 1)
    GL.glUniform4f(GL.glGetUniformLocation(cap, 'cap_base_color'), .2, .4, .6, 1)
    # A quarter turn moves the picture without changing the cap's coverage.
    GL.glTexImage2D(GL.GL_TEXTURE_2D, 0, GL.GL_RGBA, 2, 2, 0,
        GL.GL_RGBA, GL.GL_UNSIGNED_BYTE,
        bytes([255,0,0,255, 0,255,0,255, 0,0,255,255, 255,255,255,255]))
    GL.glVertexAttrib2f(uv, .6, .6)
    samples = []
    for angle in (0, math.pi/2, math.pi*2):
        GL.glUniform1f(GL.glGetUniformLocation(cap, 'cap_picture_rotation'), angle)
        GL.glDrawArrays(GL.GL_TRIANGLE_STRIP, 0, 4)
        samples.append(tuple(GL.glReadPixels(32,32,1,1,GL.GL_RGBA,GL.GL_UNSIGNED_BYTE)))
    assert samples[0] != samples[1] and samples[0] == samples[2], samples
    assert all(abs(p[3]-102) <= 1 for p in samples), 'Picture rotation changed cap coverage'
    GL.glUniform1f(GL.glGetUniformLocation(cap, 'cap_picture_rotation'), 0)
    GL.glVertexAttrib2f(uv, .5, .5)
    print('Cap picture rotation: quarter turn changes picture, full turn restores it, coverage unchanged')
    GL.glUniform1i(GL.glGetUniformLocation(cap, 'cap_picture_underlay'), 1)
    # Transparent and partially transparent PNG pixels retain the plain cap alpha.
    for picture_alpha in (0, 128, 255):
        GL.glTexImage2D(GL.GL_TEXTURE_2D, 0, GL.GL_RGBA, 1, 1, 0,
            GL.GL_RGBA, GL.GL_UNSIGNED_BYTE, bytes([255, 0, 0, picture_alpha]))
        GL.glDrawArrays(GL.GL_TRIANGLE_STRIP, 0, 4)
        pixel = tuple(GL.glReadPixels(32,32,1,1,GL.GL_RGBA,GL.GL_UNSIGNED_BYTE))
        assert abs(pixel[3]-102) <= 1, 'PNG transparency cut a hole in the cap'
        a = picture_alpha / 255
        expected = [round(102 * (.2*(1-a)+a)), round(102*.4*(1-a)), round(102*.6*(1-a))]
        assert all(abs(pixel[i]-expected[i]) <= 2 for i in range(3)), (pixel, expected)
    GL.glUniform1i(GL.glGetUniformLocation(cap, 'cap_picture_underlay'), 0)
    for picture_alpha in (0, 128, 255):
        GL.glTexImage2D(GL.GL_TEXTURE_2D, 0, GL.GL_RGBA, 1, 1, 0,
            GL.GL_RGBA, GL.GL_UNSIGNED_BYTE, bytes([255, 0, 0, picture_alpha]))
        GL.glClearColor(0,0,0,0)
        GL.glClear(GL.GL_COLOR_BUFFER_BIT)
        GL.glDrawArrays(GL.GL_TRIANGLE_STRIP, 0, 4)
        pixel = tuple(GL.glReadPixels(32,32,1,1,GL.GL_RGBA,GL.GL_UNSIGNED_BYTE))
        assert abs(pixel[3]-round(picture_alpha*.4)) <= 1, 'Disabling underlay lost PNG transparency'
    print('Cap underlay off: transparent and semi-transparent picture alpha preserved')
    GL.glUniform1i(GL.glGetUniformLocation(cap, 'cap_picture'), 0)
    GL.glTexImage2D(GL.GL_TEXTURE_2D, 0, GL.GL_RGBA, 1, 1, 0,
        GL.GL_RGBA, GL.GL_UNSIGNED_BYTE, bytes([0,255,0,255]))
    print('Cap picture: transparent PNG pixels blend onto the cap without cutting its coverage')
    # Switching ripple normals changes lighting but never cap coverage/alpha.
    samples = []
    GL.glVertexAttrib2f(uv, .55, .54)
    for bump in (0, 1):
        GL.glUniform1i(GL.glGetUniformLocation(cap, 'cap_bump_mapping'), bump)
        values = []
        for clock in (0., 1., 2., 3., 4.):
            GL.glUniform1f(GL.glGetUniformLocation(cap, 'time'), clock)
            GL.glDrawArrays(GL.GL_TRIANGLE_STRIP, 0, 4)
            values.append(tuple(GL.glReadPixels(32,32,1,1,GL.GL_RGBA,GL.GL_UNSIGNED_BYTE)))
        samples.append(values)
    assert len(set(samples[0])) == 1, 'Disabled bump mapping still animates'
    assert samples[0] != samples[1], 'Bump mapping toggle has no effect'
    assert all(abs(pixel[3]-102) <= 1 for values in samples for pixel in values)
    GL.glVertexAttrib2f(uv, .5, .5)
    GL.glUniform1f(GL.glGetUniformLocation(cap, 'time'), 0)
    print('Cap bump mapping: off is smooth/static, on changes lighting, alpha preserved')
    GL.glUniform1i(GL.glGetUniformLocation(cap,'cap_bump_mapping'),0)
    GL.glUniform1f(GL.glGetUniformLocation(cap,'cap_crystal_strength'),2)
    GL.glUniform3f(GL.glGetUniformLocation(cap,'crystal_view'),-1,1.4,0)
    crystal=[]
    for enabled in (0,1):
        GL.glUniform1i(GL.glGetUniformLocation(cap,'cap_crystal'),enabled)
        GL.glDrawArrays(GL.GL_TRIANGLE_STRIP,0,4)
        crystal.append(tuple(GL.glReadPixels(32,32,1,1,GL.GL_RGBA,GL.GL_UNSIGNED_BYTE)))
    assert crystal[0][:3]!=crystal[1][:3], 'Crystal highlights have no effect'
    assert crystal[0][3]==crystal[1][3], 'Crystal highlights changed transparency'
    GL.glUniform1i(GL.glGetUniformLocation(cap,'cap_crystal'),0)
    print('Crystal cap highlights: lighting changes, transparency preserved')
    GL.glClearColor(0,0,0,0)
    GL.glClear(GL.GL_COLOR_BUFFER_BIT)
    GL.glDrawArrays(GL.GL_TRIANGLE_STRIP,0,4)
    isolated=tuple(GL.glReadPixels(32,32,1,1,GL.GL_RGBA,GL.GL_UNSIGNED_BYTE))
    assert abs(isolated[3]-102)<=1
    GL.glClearColor(1,0,0,1)
    GL.glClear(GL.GL_COLOR_BUFFER_BIT)
    GL.glEnable(GL.GL_BLEND)
    GL.glBlendFunc(GL.GL_ONE,GL.GL_ONE_MINUS_SRC_ALPHA)
    GL.glDrawArrays(GL.GL_TRIANGLE_STRIP,0,4)
    combined=tuple(GL.glReadPixels(32,32,1,1,GL.GL_RGBA,GL.GL_UNSIGNED_BYTE))
    assert abs(combined[0]-153)<=2 and abs(combined[1]-isolated[1])<=2 and combined[3]==255
    GL.glUniform1f(GL.glGetUniformLocation(cap, 'cap_alpha'), 0)
    GL.glClear(GL.GL_COLOR_BUFFER_BIT)
    GL.glDrawArrays(GL.GL_TRIANGLE_STRIP,0,4)
    assert tuple(GL.glReadPixels(32,32,1,1,GL.GL_RGBA,GL.GL_UNSIGNED_BYTE))==(255,0,0,255)
    # Probe the actual cap mask for several polygon counts. Corner pixels
    # disappear, straight edges feather, and the center retains its opacity.
    GL.glUniform1f(GL.glGetUniformLocation(cap, 'cap_alpha'), .4)
    GL.glDisable(GL.GL_BLEND)
    for sides in [3, 4, 6, 8]:
        apothem = .5 / math.tan(math.pi / sides)
        uv_radius = math.hypot(apothem, .5)
        GL.glUniform1f(GL.glGetUniformLocation(cap, 'u_num_sides'), sides)
        GL.glUniform1f(GL.glGetUniformLocation(cap, 'cap_apothem'), apothem)
        GL.glUniform1f(GL.glGetUniformLocation(cap, 'cap_uv_radius'), uv_radius)
        def alpha_at(x, z):
            GL.glVertexAttrib2f(uv, .5+x/(2*uv_radius), .5+z/(2*uv_radius))
            GL.glClearColor(0,0,0,0)
            GL.glClear(GL.GL_COLOR_BUFFER_BIT)
            GL.glDrawArrays(GL.GL_TRIANGLE_STRIP,0,4)
            return tuple(GL.glReadPixels(32,32,1,1,GL.GL_RGBA,GL.GL_UNSIGNED_BYTE))[3]
        assert abs(alpha_at(0,0)-102)<=1
        assert alpha_at(.5,apothem)==0, sides
        feather=alpha_at(0,apothem-.006)
        assert 10 < feather < 92, (sides, feather)
        assert abs(alpha_at(0,apothem-.03)-102)<=1
    print('Cap edges: rounded transparent corners and feathered perimeter passed for 3/4/6/8 faces')
    GL.glDeleteTextures([tex])
    print('Cap shader: premultiplied glass reveals rear desktop; zero opacity leaves it untouched')
    GL.glDeleteBuffers(2, [vbo, feedback])
    for prog in [side, cap, complete]:
        GL.glDeleteProgram(prog)
    print('Deformation: GPU popout scale and matching cap edges passed for cube/cylinder/star, 3/4/6 faces, animation and lighting')
