"""Exercise texcoord patch acceptance and failure rollback through real AMD GL."""
import argparse
import ctypes as C
import json
from pathlib import Path

from battle_window_driver_reflection import SystemGL


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--wrapper', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--expect-old-failure', action='store_true')
    args = parser.parse_args()
    lib = C.WinDLL(str(args.wrapper.resolve()))
    U, I, P = C.c_uint, C.c_int, C.c_void_p
    api = lambda name, ret, *types: SystemGL.api(lib, name, ret, *types)
    setup = api('TestTexcoordSetup', None, P, P)
    track = api('TestTexcoordTrack', None, U, U, C.c_char_p)
    attach = api('TestTexcoordAttach', None, U, U)
    link = api('TestTexcoordLink', None, U)
    mirrored = api('TestTexcoordSource', C.c_char_p, U)
    cases = [
        ('xy_narrow_4_to_2', 2, 4, 'vec4(f.texcoord.xy, 0., 1.)', '', True),
        ('xyz_narrow_4_to_3', 3, 4, 'vec4(f.texcoord.xyz, 1.)', '', True),
        ('xy_widen_2_to_4', 4, 2, 'vec4(f.texcoord.xy, 0., 1.)', '', True),
        ('same_width', 4, 4, 'f.texcoord', '', True),
        ('z_rejected', 2, 4, 'vec4(f.texcoord.z)', '', False),
        ('xyz_rejected', 2, 4, 'vec4(f.texcoord.xyz, 1.)', '', False),
        ('w_rejected', 3, 4, 'vec4(f.texcoord.w)', '', False),
        ('rgb_alias_rejected', 2, 4, 'vec4(f.texcoord.rgb, 1.)', '', False),
        ('spaced_swizzle_rejected', 2, 4, 'vec4(f.texcoord \n . \n xyz, 1.)', '', False),
        ('index_rejected', 2, 4, 'vec4(f.texcoord[3])', '', False),
        ('whole_vector_rejected', 2, 4, 'f.texcoord', '', False),
        ('macro_rejected', 2, 4, 'vec4(GET_DEPTH(f.texcoord))',
         '#define GET_DEPTH(tc) ((tc).z)\n', False),
        ('inactive_z_accepted', 2, 4, 'vec4(f.texcoord.xy, 0., 1.)',
         '#if 0\nfloat inactive() { return f.texcoord.z; }\n#endif\n', True),
    ]
    results = []
    with SystemGL() as gl:
        setup(gl.gl._handle, gl.get_proc)
        create_shader = gl.proc('glCreateShader', U, U)
        source = gl.proc('glShaderSource', None, U, I, C.POINTER(C.c_char_p), C.POINTER(I))
        compile_shader = gl.proc('glCompileShader', None, U)
        shader_iv = gl.proc('glGetShaderiv', None, U, U, C.POINTER(I))
        get_source = gl.proc('glGetShaderSource', None, U, I, P, P)
        shader_log = gl.proc('glGetShaderInfoLog', None, U, I, P, P)
        create_program = gl.proc('glCreateProgram', U)
        program_iv = gl.proc('glGetProgramiv', None, U, U, C.POINTER(I))
        program_log = gl.proc('glGetProgramInfoLog', None, U, I, P, P)
        use = gl.proc('glUseProgram', None, U)
        vao = U()
        gl.proc('glGenVertexArrays', None, I, P)(1, C.byref(vao))
        gl.proc('glBindVertexArray', None, U)(vao)
        # An unmapped native window need not have readable default pixels.
        texture, framebuffer = U(), U()
        gl.proc('glGenTextures', None, I, P)(1, C.byref(texture))
        gl.proc('glBindTexture', None, U, U)(0x0de1, texture)
        gl.proc('glTexImage2D', None, U, I, I, I, I, I, U, U, P)(
            0x0de1, 0, 0x8058, 1, 1, 0, 0x1908, 0x1401, None)
        gl.proc('glGenFramebuffers', None, I, P)(1, C.byref(framebuffer))
        gl.proc('glBindFramebuffer', None, U, U)(0x8d40, framebuffer)
        gl.proc('glFramebufferTexture2D', None, U, U, U, U, I)(
            0x8d40, 0x8ce0, 0x0de1, texture, 0)
        assert gl.proc('glCheckFramebufferStatus', U, U)(0x8d40) == 0x8cd5
        gl.proc('glViewport', None, I, I, I, I)(0, 0, 1, 1)
        draw = gl.proc('glDrawArrays', None, U, I, I)
        pixels = gl.proc('glReadPixels', None, I, I, I, I, U, U, P)

        def iv(fn, obj, key):
            n = I()
            fn(obj, key, C.byref(n))
            return n.value

        def text(fn, obj):
            buf = C.create_string_buffer(8192)
            fn(obj, len(buf), None, buf)
            return buf.value

        def shader(kind, raw):
            obj = create_shader(kind)
            data, size = C.c_char_p(raw), I(len(raw))
            source(obj, 1, C.byref(data), C.byref(size))
            compile_shader(obj)
            assert iv(shader_iv, obj, 0x8b81), text(shader_log, obj)
            track(obj, kind, raw)
            return obj

        def vertex(width):
            values = ','.join(['0.25', '0.75', '0.5', '1.0'][:width])
            return (f'#version 450\nout VertexData {{ vec{width} texcoord; }} v;\n'
                    f'void main() {{ v.texcoord=vec{width}({values}); '
                    'vec2 p=vec2((gl_VertexID << 1) & 2, gl_VertexID & 2); '
                    'gl_Position=vec4(p*2.-1.,0.,1.); }\n').encode()

        def program(vs, fs):
            obj = create_program()
            attach(obj, vs)
            attach(obj, fs)
            link(obj)
            return obj

        for name, vt, ft, expr, prefix, accepted in cases:
            raw = (f'#version 450\nin VertexData {{ vec{ft} texcoord; }} f;\n'
                   'layout(location=0) out vec4 color;\n' + prefix +
                   f'void main() {{ color={expr}; }}\n').encode()
            vs, fs = shader(0x8b31, vertex(vt)), shader(0x8b30, raw)
            obj = program(vs, fs)
            linked = bool(iv(program_iv, obj, 0x8b82))
            compiled = bool(iv(shader_iv, fs, 0x8b81))
            driver = text(get_source, fs)
            mirror = mirrored(fs)
            row = dict(name=name,accepted=accepted,linked=linked,compiled=compiled,
                       driver_equals_mirror=driver == mirror)
            if accepted:
                assert linked and compiled and driver == mirror, (name, text(program_log, obj))
                expected = raw.replace(f'vec{ft} texcoord;'.encode(), f'vec{vt} texcoord;'.encode(), 1)
                assert driver == expected, name
                use(obj)
                draw(4, 0, 3)
                pixel = (C.c_ubyte * 4)()
                pixels(0, 0, 1, 1, 0x1908, 0x1401, pixel)
                row['pixel'] = list(pixel)
                expected_pixel = [64, 191, 128 if name in ('xyz_narrow_4_to_3', 'same_width') else 0, 255]
                assert all(abs(x-y) <= 1 for x,y in zip(pixel, expected_pixel)), (name, list(pixel))
            elif args.expect_old_failure:
                assert not linked and not compiled and driver != raw and mirror == raw, name
                row['old_corruption_reproduced'] = True
                row['link_log'] = text(program_log, obj).decode(errors='replace')
            else:
                # A real interface mismatch may still fail to link. The repair
                # guarantees the rejected patch cannot poison this shader.
                assert compiled and driver == raw and mirror == raw, name
                good_vs = shader(0x8b31, vertex(ft))
                good = program(good_vs, fs)
                assert iv(program_iv, good, 0x8b82), (name, text(program_log, good))
                row['compatible_program_reuses_restored_shader'] = True
            results.append(row)
        assert gl.proc('glGetError', U)() == 0
        gpu = gl.proc('glGetString', C.c_char_p, U)(0x1f01).decode()
    result = dict(gpu=gpu,old_failure_control=args.expect_old_failure,cases=results,passed=True)
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(dict(passed=True,cases=len(results),gpu=gpu)))


if __name__ == '__main__':
    main()
