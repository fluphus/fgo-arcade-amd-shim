"""Run the production split-blit wrapper in a hidden, separate AMD GL context."""
import argparse
import ctypes as C
import json
from pathlib import Path

import numpy as np
from battle_window_driver_reflection import SystemGL

U, I, P, F, B = C.c_uint, C.c_int, C.c_void_p, C.c_float, C.c_ubyte


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('wrapper', type=Path)
    ap.add_argument('--expect-broken', action='store_true')
    ap.add_argument('--output', type=Path, required=True)
    args = ap.parse_args()
    report = {'cases': []}
    with SystemGL() as gl:
        fn = gl.proc
        dll = C.CDLL(str(args.wrapper.resolve()))
        resolve_type = C.WINFUNCTYPE(P, C.c_char_p)
        resolver = resolve_type(gl.get_proc)
        gl.api(dll, 'TestSplitConfigure', None, P, resolve_type)(gl.gl._handle, resolver)
        split = gl.api(dll, 'TestSplit', None, U, U, U, U)
        error = fn('glGetError', U)
        get = fn('glGetIntegerv', None, U, P)
        bind = fn('glBindFramebuffer', None, U, U)
        draw_buffers = fn('glNamedFramebufferDrawBuffers', None, U, I, P)
        clear_color = fn('glClearNamedFramebufferfv', None, U, U, I, P)
        clear_depth = fn('glClearNamedFramebufferfi', None, U, U, I, F, I)
        read = fn('glGetTextureImage', None, U, I, U, U, I, P)

        def integer(name):
            out = I()
            get(name, C.byref(out))
            return out.value

        def texture(fmt):
            out = U()
            fn('glCreateTextures', None, U, I, P)(0x0DE1, 1, C.byref(out))
            fn('glTextureStorage2D', None, U, I, U, I, I)(out, 1, fmt, 32, 24)
            return out.value

        def framebuffer(fmt):
            fbo = U()
            fn('glCreateFramebuffers', None, I, P)(1, C.byref(fbo))
            colors = [texture(0x8058) for _ in range(3)]
            depth = texture(fmt)
            attach = fn('glNamedFramebufferTexture', None, U, U, U, I)
            for i, tex in enumerate(colors):
                attach(fbo, 0x8CE0 + i, tex, 0)
            attach(fbo, 0x821A, depth, 0)
            draw_buffers(fbo, 3, (U * 3)(0x8CE0, 0x8CE1, 0x8CE2))
            fn('glNamedFramebufferReadBuffer', None, U, U)(fbo, 0x8CE0)
            assert fn('glCheckNamedFramebufferStatus', U, U, U)(fbo, 0x8D40) == 0x8CD5
            return fbo.value, colors, depth

        def pixels(tex):
            out = np.empty((24, 32, 4), dtype=np.uint8)
            read(tex, 0, 0x1908, 0x1401, out.nbytes, out.ctypes.data)
            return out

        report['renderer'] = fn('glGetString', C.c_char_p, U)(0x1F01).decode()
        for source_fmt in (0x88F0, 0x8CAD):
            src, src_colors, src_depth = framebuffer(source_fmt)
            dst, dst_colors, dst_depth = framebuffer(0x88F0)
            fn('glDepthMask', None, B)(1)
            clear_color(src, 0x1800, 0, (F * 4)(0.25, 0.5, 0.75, 1.0))
            clear_depth(src, 0x84F9, 0, 0.75, 90)
            source_color = pixels(src_colors[0])
            for outputs in ((0x8CE0, 0x8CE1, 0x8CE2), (0, 0x8CE2, 0x8CE1)):
                fn('glDepthMask', None, B)(1)
                draw_buffers(dst, 3, (U * 3)(0x8CE0, 0x8CE1, 0x8CE2))
                for slot in range(3):
                    clear_color(dst, 0x1800, slot, (F * 4)(0, 0, 0, 1))
                clear_depth(dst, 0x84F9, 0, 0.125, 17)
                original_colors = [pixels(tex) for tex in dst_colors]
                draw_buffers(dst, 3, (U * 3)(*outputs))
                bind(0x8CA8, src)
                bind(0x8CA9, dst)
                fn('glDepthMask', None, B)(0)
                assert error() == 0
                split(src, dst, src_depth, dst_depth)
                assert error() == 0
                after = tuple(integer(0x8825 + i) for i in range(3))
                assert integer(0x8CAA) == src and integer(0x8CA6) == dst
                assert integer(0x0B72) == 0
                assert np.array_equal(pixels(src_colors[0]), source_color)
                for slot, tex in enumerate(dst_colors):
                    expected = original_colors[slot].copy()
                    if 0x8CE0 + slot in outputs:
                        expected[5:17, 4:20] = source_color[3:15, 2:18]
                    assert np.array_equal(pixels(tex), expected), ('color copy', source_fmt, slot)
                depth = np.empty((24, 32), dtype=np.float32)
                read(dst_depth, 0, 0x1902, 0x1406, depth.nbytes, depth.ctypes.data)
                expected_depth = np.full((24, 32), 0.125, dtype=np.float32)
                expected_depth[5:17, 4:20] = 0.75
                depth_error = float(np.max(np.abs(depth - expected_depth)))
                assert depth_error < 2 / 16777215, (hex(source_fmt), outputs, depth_error,
                    float(depth[0, 0]), float(depth[5, 4]), float(depth[16, 19]))
                packed = np.empty((24, 32), dtype=np.uint32)
                read(dst_depth, 0, 0x84F9, 0x84FA, packed.nbytes, packed.ctypes.data)
                assert np.all((packed & 255) == 17), 'stencil changed'
                # A following operation must still reach every selected output.
                clear_color(dst, 0x1800, 1, (F * 4)(1, 0, 0, 1))
                selected_texture = dst_colors[outputs[1] - 0x8CE0]
                following_write_ok = np.all(pixels(selected_texture) == [255, 0, 0, 255])
                state_ok = after == outputs
                report['cases'].append(dict(source_format=hex(source_fmt), before=list(outputs),
                    after=list(after), state_preserved=state_ok,
                    following_output_write=bool(following_write_ok), color_depth_stencil_ok=True))
                assert state_ok != args.expect_broken
                assert bool(following_write_ok) != args.expect_broken
                assert error() == 0
            fn('glDeleteFramebuffers', None, I, P)(2, (U * 2)(src, dst))
            textures = src_colors + dst_colors + [src_depth, dst_depth]
            fn('glDeleteTextures', None, I, P)(len(textures), (U * len(textures))(*textures))
    args.output.write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps(report, separators=(',', ':')))


if __name__ == '__main__':
    main()
