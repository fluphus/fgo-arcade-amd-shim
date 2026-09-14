"""Reflect actual captured AMD uniform layouts; no draws or game DLL loaded."""
import ctypes as C
from ctypes import wintypes as W
import json
import os
from pathlib import Path


class SystemGL:
    def __init__(self):
        self.user = C.WinDLL('user32', use_last_error=True)
        self.gdi = C.WinDLL('gdi32', use_last_error=True)
        self.gl = C.WinDLL(str(Path(os.environ['SystemRoot']) / 'System32/opengl32.dll'))
        self.window = self.dc = self.context = None

    @staticmethod
    def api(lib, name, result, *args):
        fn = getattr(lib, name)
        fn.restype, fn.argtypes = result, list(args)
        return fn

    def proc(self, name, result, *args):
        address = self.get_proc(name.encode())
        if address and address not in (1, 2, 3, C.c_void_p(-1).value):
            return C.WINFUNCTYPE(result, *args)(address)
        return self.api(self.gl, name, result, *args)

    def __enter__(self):
        class PFD(C.Structure):
            _fields_ = [('size', W.WORD), ('version', W.WORD), ('flags', W.DWORD),
                        ('pixel_type', W.BYTE), ('color_bits', W.BYTE), ('components', W.BYTE * 18),
                        ('layer_mask', W.DWORD), ('visible_mask', W.DWORD), ('damage_mask', W.DWORD)]
        create = self.api(self.user, 'CreateWindowExW', W.HWND, W.DWORD, W.LPCWSTR, W.LPCWSTR,
                          W.DWORD, C.c_int, C.c_int, C.c_int, C.c_int, W.HWND, W.HMENU, W.HINSTANCE, C.c_void_p)
        self.window = create(0, 'STATIC', 'Captured GL layout', 0, 0, 0, 16, 16, None, None, None, None)
        if not self.window: raise C.WinError(C.get_last_error())
        self.dc = self.api(self.user, 'GetDC', W.HDC, W.HWND)(self.window)
        pf = PFD()
        pf.size, pf.version, pf.flags, pf.color_bits = C.sizeof(pf), 1, 0x24, 32
        choose = self.api(self.gdi, 'ChoosePixelFormat', C.c_int, W.HDC, C.POINTER(PFD))
        set_format = self.api(self.gdi, 'SetPixelFormat', W.BOOL, W.HDC, C.c_int, C.POINTER(PFD))
        fmt = choose(self.dc, C.byref(pf))
        if not fmt or not set_format(self.dc, fmt, C.byref(pf)): raise C.WinError(C.get_last_error())
        self.context = self.api(self.gl, 'wglCreateContext', C.c_void_p, W.HDC)(self.dc)
        self.make_current = self.api(self.gl, 'wglMakeCurrent', W.BOOL, W.HDC, C.c_void_p)
        if not self.context or not self.make_current(self.dc, self.context): raise C.WinError(C.get_last_error())
        self.get_proc = self.api(self.gl, 'wglGetProcAddress', C.c_void_p, C.c_char_p)
        return self

    def __exit__(self, *args):
        if self.context:
            self.make_current(None, None)
            self.api(self.gl, 'wglDeleteContext', W.BOOL, C.c_void_p)(self.context)
        if self.dc: self.api(self.user, 'ReleaseDC', C.c_int, W.HWND, W.HDC)(self.window, self.dc)
        if self.window: self.api(self.user, 'DestroyWindow', W.BOOL, W.HWND)(self.window)


def main():
    root = Path('build/battle-window-compare-20260909')
    inventory = json.loads((root / 'amd-post/inventory.json').read_text())
    U, I, P = C.c_uint, C.c_int, C.c_void_p
    result = {}
    with SystemGL() as gl:
        create_shader = gl.proc('glCreateShader', U, U)
        shader_source = gl.proc('glShaderSource', None, U, I, C.POINTER(C.c_char_p), C.POINTER(I))
        compile_shader = gl.proc('glCompileShader', None, U)
        shader_iv = gl.proc('glGetShaderiv', None, U, U, C.POINTER(I))
        shader_log = gl.proc('glGetShaderInfoLog', None, U, I, C.POINTER(I), P)
        create_program = gl.proc('glCreateProgram', U)
        attach = gl.proc('glAttachShader', None, U, U)
        link = gl.proc('glLinkProgram', None, U)
        program_iv = gl.proc('glGetProgramiv', None, U, U, C.POINTER(I))
        program_log = gl.proc('glGetProgramInfoLog', None, U, I, C.POINTER(I), P)
        uniform_iv = gl.proc('glGetActiveUniformsiv', None, U, I, C.POINTER(U), U, C.POINTER(I))
        uniform_name = gl.proc('glGetActiveUniformName', None, U, U, I, C.POINTER(I), P)
        block_iv = gl.proc('glGetActiveUniformBlockiv', None, U, U, U, C.POINTER(I))
        block_name = gl.proc('glGetActiveUniformBlockName', None, U, U, I, C.POINTER(I), P)
        error = gl.proc('glGetError', U)
        shaders = {}
        for original in (253, 258, 1148, 1154, 1172, 1100, 434, 2154, 2637):
            program = create_program()
            for sid in inventory['usage'][str(original)]['shaders']:
                if sid not in shaders:
                    shader = create_shader(inventory['shaders'][str(sid)]['type'])
                    raw = (root / f'amd-post/shader_{sid}.glsl').read_bytes()
                    src, length = C.c_char_p(raw), I(len(raw))
                    shader_source(shader, 1, C.byref(src), C.byref(length))
                    compile_shader(shader)
                    ok, log = I(), C.create_string_buffer(16384)
                    shader_iv(shader, 0x8b81, C.byref(ok)); shader_log(shader, len(log), None, log)
                    if not ok.value: raise RuntimeError((sid, log.value))
                    shaders[sid] = shader
                attach(program, shaders[sid])
            link(program)
            ok, log = I(), C.create_string_buffer(16384)
            program_iv(program, 0x8b82, C.byref(ok)); program_log(program, len(log), None, log)
            if not ok.value: raise RuntimeError((original, log.value))
            blocks, uniforms, count = {}, {}, I()
            program_iv(program, 0x8a36, C.byref(count))
            for index in range(count.value):
                name = C.create_string_buffer(1024)
                block_name(program, index, len(name), None, name)
                data = {}
                for field, enum in (('binding', 0x8a3f), ('bytes', 0x8a40)):
                    value = I(); block_iv(program, index, enum, C.byref(value)); data[field] = value.value
                blocks[name.value.decode()] = dict(index=index, **data)
            program_iv(program, 0x8b86, C.byref(count))
            for index in range(count.value):
                name = C.create_string_buffer(1024)
                uniform_name(program, index, len(name), None, name)
                idx, data = U(index), {}
                for field, enum in (('offset', 0x8a3b), ('stride', 0x8a3c), ('block', 0x8a3a), ('size', 0x8a38)):
                    value = I(); uniform_iv(program, 1, C.byref(idx), enum, C.byref(value)); data[field] = value.value
                if data['block'] >= 0: uniforms[name.value.decode()] = data
            result[original] = dict(blocks=blocks, uniforms=uniforms)
            assert error() == 0
        identity = gl.proc('glGetString', C.c_char_p, U)(0x1f01).decode()
    (root / 'driver-uniform-layouts.json').write_text(json.dumps(dict(driver=identity, programs=result), indent=2))
    print(json.dumps({p: dict(blocks=v['blocks'], selected={n:d for n,d in v['uniforms'].items()
          if 'handle' in n or n.endswith('m_transform[0]') or 'opacity_map' in n}) for p,v in result.items()}, indent=2))


if __name__ == '__main__': main()
