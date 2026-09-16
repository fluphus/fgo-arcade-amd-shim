"""Check captured packed castle UV/color through actual shim attribute replay."""
import argparse
import ctypes as C
import json
from pathlib import Path

import numpy as np
from battle_window_driver_reflection import SystemGL

U, I, P, S = C.c_uint, C.c_int, C.c_void_p, C.c_ssize_t


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('wrapper', type=Path)
    parser.add_argument('capture', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    fixture = args.capture / 'sample.json'
    if fixture.is_file():
        records = json.loads(fixture.read_text(encoding='utf-8'))['rows']
        read_blob = lambda identifier: (args.capture / f'blob-{identifier}.bin').read_bytes()
    else:
        from battle_scene_capture_audit import Capture
        capture = Capture(args.capture)
        records = capture.records(2431, 'buffer')
        read_blob = capture.blob
    proper = next(r for r in records if r['role'] == 'vertex_prefix' and r['command'] == 0)
    shifted = next(r for r in records if r['role'] == 'vertex_prefix' and r['command'] == 1)
    raw, wrong = read_blob(proper['blob']), read_blob(shifted['blob'])
    assert shifted['offset'] == proper['offset'] + 12 and raw[12:] == wrong[:-12]
    count = len(raw) // 32
    position = np.ndarray((count, 3), '<f4', raw, 0, (32, 4)).copy()
    uv = np.ndarray((count, 4), '<u2', raw, 20, (32, 2)).astype(np.float32)
    color = np.ndarray((count, 4), np.uint8, raw, 28, (32, 1)).astype(np.float32) / 255
    wrong_uv = np.ndarray((count, 4), '<u2', wrong, 20, (32, 2)).astype(np.float32)
    wrong_color = np.ndarray((count, 4), np.uint8, wrong, 28, (32, 1)).astype(np.float32) / 255
    with SystemGL() as gl:
        proc = gl.proc
        dll = C.WinDLL(str(args.wrapper.resolve()))
        resolve_type = C.WINFUNCTYPE(P, C.c_char_p)
        resolve = resolve_type(gl.get_proc)
        gl.api(dll, 'TestCastleConfigure', None, P, resolve_type)(gl.gl._handle, resolve)
        replay = gl.api(dll, 'TestCastleReplay', I, U, U, I, S, S, I)
        shader = proc('glCreateShader', U, U)(0x8b31)
        code = b'''#version 450
layout(location=0) in vec3 pos;
layout(location=1) in vec4 normal;
layout(location=2) in vec4 tangent;
layout(location=3) in vec2 uv0;
layout(location=4) in vec2 uv1;
layout(location=7) in vec4 color;
out vec3 p; out vec4 n; out vec4 t; out vec4 uv; out vec4 c;
void main(){p=pos;n=normal;t=tangent;uv=vec4(uv0,uv1);c=color;gl_Position=vec4(pos,1);}
'''
        text, length = C.c_char_p(code), I(len(code))
        proc('glShaderSource', None, U, I, C.POINTER(C.c_char_p), C.POINTER(I))(shader, 1, C.byref(text), C.byref(length))
        proc('glCompileShader', None, U)(shader)
        program = proc('glCreateProgram', U)()
        proc('glAttachShader', None, U, U)(program, shader)
        varyings = (C.c_char_p * 5)(b'p', b'n', b't', b'uv', b'c')
        proc('glTransformFeedbackVaryings', None, U, I, C.POINTER(C.c_char_p), U)(program, 5, varyings, 0x8c8c)
        proc('glLinkProgram', None, U)(program)
        linked = I()
        proc('glGetProgramiv', None, U, U, P)(program, 0x8b82, C.byref(linked))
        assert linked.value
        proc('glUseProgram', None, U)(program)

        def buffer(data):
            name = U()
            proc('glCreateBuffers', None, I, P)(1, C.byref(name))
            data = data.ljust(524288, b'\0')
            proc('glNamedBufferData', None, U, S, P, U)(name, len(data), data, 0x88e4)
            return name.value

        data = raw + wrong[-12:]
        vertex, relocated = buffer(data), buffer(bytes(64) + data)
        feedback = buffer(bytes(count * 19 * 4))
        proc('glBindBufferBase', None, U, U, U)(0x8c8e, 0, feedback)
        proc('glEnable', None, U)(0x8c89)
        rows, outputs = [], {}
        cases = [('legacy', 0, vertex, 0, 0, 0), ('fixed', 1, vertex, 0, 0, 0),
                 ('cached', 1, vertex, 0, 0, 1), ('relocated', 1, relocated, 512, 64, 0),
                 ('return_to_original', 1, vertex, 0, 0, 0)]
        for name, enabled, vb, core, nv, repeat in cases:
            selected = replay(program, vb, enabled, core, nv, repeat)
            assert bool(selected) == bool(enabled), name
            proc('glBeginTransformFeedback', None, U)(0)
            proc('glDrawArrays', None, U, I, I)(0, 0, count)
            proc('glEndTransformFeedback', None)()
            output = np.empty((count, 19), np.float32)
            proc('glGetNamedBufferSubData', None, U, S, S, P)(feedback, 0, output.nbytes, output.ctypes.data)
            assert proc('glGetError', U)() == 0, name
            assert np.array_equal(output[:, :3], position), name
            wanted_uv, wanted_color = (uv, color) if enabled else (wrong_uv, wrong_color)
            assert np.array_equal(output[:, 11:15], wanted_uv), name
            assert np.allclose(output[:, 15:19], wanted_color, rtol=0, atol=1e-7), name
            outputs[name] = output
            rows.append(dict(case=name,shared_route=bool(selected),
                incorrect_uv_vertices=int(np.any(output[:,11:15]!=uv,axis=1).sum()),
                incorrect_color_vertices=int(np.any(np.abs(output[:,15:19]-color)>1e-7,axis=1).sum())))
        for name in outputs:
            assert np.array_equal(outputs[name][:,:11], outputs['legacy'][:,:11]), name
        assert rows[0]['incorrect_uv_vertices'] == rows[0]['incorrect_color_vertices'] == count
        result = dict(passed=True,gpu=proc('glGetString',C.c_char_p,U)(0x1f01).decode(),
                      captured_sequence=2431,vertices=count,position_normal_tangent_unchanged=True,cases=rows)
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(result))


if __name__ == '__main__':
    main()
