"""Compare original captured model shaders with ordinary-sampler lowering."""
import argparse
import ctypes as C
import hashlib
import json
import math
from pathlib import Path
import struct
import statistics
import time

from battle_window_driver_reflection import SystemGL
from sampler_failure_metadata import (FAILURE_OFFSET, RANGE_COUNT_OFFSET, RANGES_OFFSET,
                                     read_sampler_failures, sampler_failure_delta)

U, I, P, Q = C.c_uint, C.c_int, C.c_void_p, C.c_ulonglong
ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUT = ROOT / 'build/regular-samplers-20260911'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--npr-program', type=int, choices=(764, 1748))
    parser.add_argument('--source-root', type=Path, help='Captured NPR shader directory with inventory.json.')
    parser.add_argument('--fixture-root', type=Path, help='Directory containing program_1156_shader_1154/1155.glsl.')
    parser.add_argument('--runner', type=Path, help='Compiled driver_residency_benchmark.c DLL.')
    parser.add_argument('--output', type=Path, default=DEFAULT_OUT)
    parser.add_argument('--wrapper', type=Path)
    parser.add_argument('--before', type=Path, help='Compare previous integrated sampler preparation in the same context.')
    args = parser.parse_args()
    OUT = args.output.resolve()
    OUT.mkdir(parents=True, exist_ok=True)
    wrapper = C.WinDLL(str((args.wrapper or OUT / 'wrapper.dll').resolve()))
    layout = (I * 4)()
    SystemGL.api(wrapper, 'TestRegularMetadataLayout', None, P)(layout)
    assert list(layout) == [4912, FAILURE_OFFSET, RANGE_COUNT_OFFSET, RANGES_OFFSET], list(layout)
    rewrite = SystemGL.api(wrapper, 'TestRegularSource', P, C.c_char_p)
    release = SystemGL.api(wrapper, 'TestRegularFree', None, P)
    sources = []
    if args.npr_program:
        source_root = args.source_root or ROOT / 'build/battle-shadow-alpha-sources-25556-195812'
        inventory = json.loads((source_root / 'inventory.json').read_text())
        stages = next(row['shaders'] for row in inventory['programs'] if row['program'] == args.npr_program)
        stages = sorted(stages, key=lambda row: row['type'], reverse=True)
        paths = [source_root / stage['file'] for stage in stages]
        for path, stage in zip(paths, stages):
            assert hashlib.sha256(path.read_bytes()).hexdigest() == stage['sha256']
    else:
        source_root = args.fixture_root or ROOT / 'build/melt-formation-20260910'
        paths = [source_root / f'program_1156_shader_{sid}.glsl' for sid in (1154, 1155)]
    for path in paths:
        source = path.read_bytes()
        ptr = rewrite(source)
        assert ptr, str(path)
        lowered = C.string_at(ptr)
        release(ptr)
        (OUT / ('lowered_' + path.name)).write_bytes(lowered)
        sources.append((source, lowered))
    assert sources[0][0] == sources[0][1], 'final vertex source must remain unchanged'
    report = dict(evidence_program=args.npr_program or 1156, vertex_byte_identical=True)
    with SystemGL() as gl:
        def fn(name, result, *args): return gl.proc(name, result, *args)
        error = fn('glGetError', U)
        def check(label):
            code = error()
            assert code == 0, (label, hex(code))
        def compile_program(index):
            program = fn('glCreateProgram', U)()
            for kind, pair in zip((0x8B31, 0x8B30), sources):
                shader = fn('glCreateShader', U, U)(kind)
                src = C.c_char_p(pair[index])
                fn('glShaderSource', None, U, I, C.POINTER(C.c_char_p), P)(shader, 1, C.byref(src), None)
                fn('glCompileShader', None, U)(shader)
                ok, log = I(), C.create_string_buffer(16384)
                fn('glGetShaderiv', None, U, U, C.POINTER(I))(shader, 0x8B81, C.byref(ok))
                fn('glGetShaderInfoLog', None, U, I, P, P)(shader, len(log), None, log)
                assert ok.value, (index, kind, log.value)
                fn('glAttachShader', None, U, U)(program, shader)
            fn('glLinkProgram', None, U)(program)
            ok, log = I(), C.create_string_buffer(16384)
            fn('glGetProgramiv', None, U, U, C.POINTER(I))(program, 0x8B82, C.byref(ok))
            fn('glGetProgramInfoLog', None, U, I, P, P)(program, len(log), None, log)
            assert ok.value, (index, log.value)
            return program
        programs = [compile_program(i) for i in range(2)]
        def reflect(program):
            count = I()
            fn('glGetProgramiv', None, U, U, C.POINTER(I))(program, 0x8B86, C.byref(count))
            result = {}
            for idx in range(count.value):
                name, length, size, kind = C.create_string_buffer(512), I(), I(), U()
                fn('glGetActiveUniform', None, U, U, I, P, C.POINTER(I), C.POINTER(U), P)(program, idx, len(name), None, C.byref(size), C.byref(kind), name)
                data = dict(type=kind.value, size=size.value)
                for field, enum in (('block', 0x8A3A), ('offset', 0x8A3B), ('stride', 0x8A3C)):
                    value = I(); uniform = U(idx)
                    fn('glGetActiveUniformsiv', None, U, I, C.POINTER(U), U, C.POINTER(I))(program, 1, C.byref(uniform), enum, C.byref(value))
                    data[field] = value.value
                data['location'] = fn('glGetUniformLocation', I, U, C.c_char_p)(program, name.value)
                if data['block'] < 0 and kind.value != 0x8B52:
                    value = I()
                    fn('glGetUniformiv', None, U, I, C.POINTER(I))(program, data['location'], C.byref(value))
                    data['initial'] = value.value
                result[name.value.decode()] = data
            return result
        reflection = [reflect(p) for p in programs]
        install = SystemGL.api(wrapper, 'TestRegularInstall', I, P, P, U, C.c_char_p, C.c_char_p)
        slots = install(gl.gl._handle, gl.get_proc, programs[0], sources[0][0], sources[1][0])
        assert slots > 0, 'private program rejected'
        shadow_note = SystemGL.api(wrapper, 'TestRegularShadow', None, U, C.c_ssize_t, P)
        uniform_note = SystemGL.api(wrapper, 'TestRegularUniform', None, U, I, I, P)
        sampler_note = SystemGL.api(wrapper, 'TestRegularSampler', None, U, I, I)
        note_handle = SystemGL.api(wrapper, 'TestRegularHandle', None, Q, U, U)
        known_handles = {}
        def handle_note(handle, texture_name, sampler_name):
            note_handle(handle, texture_name, sampler_name)
            known_handles[handle] = (texture_name, sampler_name)
        integrated_raw = SystemGL.api(wrapper, 'TestRegularDraw', I, U, U)
        seed_state = SystemGL.api(wrapper, 'TestRegularSeedState', None, U)
        disable_cache = SystemGL.api(wrapper, 'TestRegularDisableCache', None)
        def integrated_cached(program, count):
            seed_state(program)
            result = integrated_raw(program, count)
            disable_cache()
            return result
        integrated = integrated_cached
        report['reflection'] = reflection
        for name, data in reflection[0].items():
            if data['block'] >= 0:
                assert name in reflection[1], name
                for key in ('type', 'offset', 'stride', 'size'):
                    assert data[key] == reflection[1][name][key], (name, key)
        (OUT / 'reflection.json').write_text(json.dumps(report, indent=2))
        check('reflection')
        layout_equal = SystemGL.api(wrapper, 'TestRegularUniformLayoutEqual', I, U, U, U, U)
        layout_fields = (0x8A37, 0x8A38, 0x8A3B, 0x8A3C, 0x8A3D, 0x8A3E)
        layout_rows = []
        for name, data in reflection[0].items():
            if data['block'] < 0:
                continue
            indices, values = [], []
            text = C.c_char_p(name.encode())
            for program in programs:
                index = U(0xffffffff)
                fn('glGetUniformIndices', None, U, I, C.POINTER(C.c_char_p), C.POINTER(U))(
                    program, 1, C.byref(text), C.byref(index))
                assert index.value != 0xffffffff, name
                fields = []
                for field in layout_fields:
                    value = I()
                    fn('glGetActiveUniformsiv', None, U, I, C.POINTER(U), U, C.POINTER(I))(
                        program, 1, C.byref(index), field, C.byref(value))
                    fields.append(value.value)
                indices.append(index.value)
                values.append(fields)
            assert values[0] == values[1], (name, values)
            assert layout_equal(programs[0], indices[0], programs[1], indices[1]) == 1, name
            layout_rows.append((indices, values))
        mismatches = 0
        for first, second in zip(layout_rows, layout_rows[1:]):
            expected = first[1][0] == second[1][1]
            actual = layout_equal(programs[0], first[0][0], programs[1], second[0][1])
            assert actual == int(expected), (first, second, actual)
            mismatches += not expected
        assert mismatches > 0, 'layout regression must exercise unequal member layouts'
        report['batched_uniform_layout'] = dict(matching_members=len(layout_rows),
            mismatched_pairs_rejected=mismatches, checked_properties=len(layout_fields))
        check('batched uniform layout parity')
        query_indices = SystemGL.api(wrapper, 'TestRegularUniformIndices', I, U, I,
                                    C.POINTER(C.c_char_p), C.POINTER(U), I)
        texts = list(reflection[1]) + ['_not_a_uniform_']
        names = (C.c_char_p * len(texts))(*(text.encode() for text in texts))
        direct, cached = (U * len(texts))(), (U * len(texts))()
        query_indices(programs[1],len(texts),names,direct,0)
        misses = query_indices(programs[1],len(texts),names,cached,1)
        assert list(direct) == list(cached) and misses == 1
        report['uniform_name_lookup'] = dict(matching_names=len(texts)-1,
                                            missing_name_fallbacks=misses)
        check('uniform name lookup parity')
        textures = {}
        def texture(target, internal, shadow=False, side=1):
            name = U(); fn('glCreateTextures', None, U, I, C.POINTER(U))(target, 1, C.byref(name))
            if target == 0x8C2A:
                buffer = U(); fn('glCreateBuffers', None, I, C.POINTER(U))(1, C.byref(buffer))
                fn('glNamedBufferData', None, U, C.c_ssize_t, P, U)(buffer, 16384, (C.c_ubyte * 16384)(), 0x88E4)
                fn('glTextureBuffer', None, U, U, U)(name, 0x8D82, buffer)
            else:
                if target in (0x8C1A, 0x806F):
                    fn('glTextureStorage3D', None, U, I, U, I, I, I)(name, 1, internal, 1, 1, 1)
                else:
                    fn('glTextureStorage2D', None, U, I, U, I, I)(name, 1, internal, side, side)
                pixel = (C.c_float * 4)(0.25, 0.5, 0.75, 1)
                fn('glClearTexImage', None, U, I, U, U, P)(name, 0, 0x1902 if shadow else 0x1908, 0x1406, pixel)
                for parameter, value in ((0x2801, 0x2600), (0x2800, 0x2600)):
                    fn('glTextureParameteri', None, U, U, I)(name, parameter, value)
                if shadow: fn('glTextureParameteri', None, U, U, I)(name, 0x884C, 0x884E)
            handle = fn('glGetTextureHandleARB', Q, U)(name)
            fn('glMakeTextureHandleResidentARB', None, Q)(handle)
            handle_note(handle, name, 0)
            check('texture setup')
            return name.value, handle
        for kind, target, internal, shadow in ((0x8B5E, 0xDE1, 0x8058, False),
                (0x8B60, 0x8513, 0x8058, False), (0x8DD0, 0x8C2A, 0, False),
                (0x8B62, 0xDE1, 0x8CAC, True), (0x8DC4, 0x8C1A, 0x8CAC, True),
                (0x8B5F, 0x806F, 0x8058, False)):
            textures[kind] = texture(target, internal, shadow)
        block_data = {}
        for data in reflection[0].values():
            if data['block'] >= 0: block_data[data['block']] = bytearray(4096)
        for name, data in reflection[0].items():
            if data['block'] < 0: continue
            raw, offset = block_data[data['block']], data['offset']
            if name == '_amdshim_map_handle_pairs[0]':
                for index in range(18): struct.pack_into('<Q', raw, offset + 8 * index, textures[0x8B5E][1])
            elif name == '_amdshim_map_handle_last': struct.pack_into('<Q', raw, offset, textures[0x8B5E][1])
            elif 'map_handle' in name:
                kind = 0x8DC4 if name == 'g_local_shadow_map_handle' else 0x8B62 if 'cascaded' in name or name == 'g_unique_shadow_map_handle' else 0x8B5E
                if name != 'g_planar_reflection_map_handle': struct.pack_into('<Q', raw, offset, textures[kind][1])
        def set4(name, value, index=0):
            d = reflection[0][name]
            struct.pack_into('<4f', block_data[d['block']], d['offset'] + index * d['stride'], *value)
        set4('g_eye', (0, 0, 2, 1))
        set4('g_parallel_light_dir', (0, 0, 1, 0.5))
        set4('g_parallel_light_colors[0]', (0.5, 0.5, 0.5, 1), 1)
        set4('g_cloud_shadow_texcoords[0]', (1, 1, 1 / 16, 1 / 16), 1)
        for i in range(10): set4('g_color_gains[0]', (1, 1, 1, 1), i)
        if args.npr_program:
            for row in range(6):
                set4('g_npr_tone[0]', tuple(float(axis == row % 3) for axis in range(4)), row)
            set4('g_parallel_light_colors[0]', (0.5, 0.6, 0.7, 1))
            set4('g_tangent_transforms[0]', (1, 1, 1, 0), 3)
            set4('g_ao_intensities', (1, 0, 0, 0))
            opaque, opaque_handle = texture(0xDE1, 0x8058)
            fn('glClearTexImage', None, U, I, U, U, P)(opaque, 0, 0x1908, 0x1406, (C.c_float * 4)(1, 1, 1, 1))
            material_member = reflection[0]['_amdshim_map_handle_pairs[0]']
            struct.pack_into('<Q', block_data[material_member['block']], material_member['offset'] + 8 * 16, opaque_handle)
        block_buffers = {}
        for block, raw in block_data.items():
            binding = I()
            fn('glGetActiveUniformBlockiv', None, U, U, U, C.POINTER(I))(programs[0], block, 0x8A3F, C.byref(binding))
            buffer = U(); fn('glCreateBuffers', None, I, C.POINTER(U))(1, C.byref(buffer))
            fn('glNamedBufferData', None, U, C.c_ssize_t, P, U)(buffer, len(raw), C.c_char_p(bytes(raw)), 0x88E4)
            fn('glBindBufferBase', None, U, U, U)(0x8A11, binding.value, buffer)
            shadow_note(buffer, len(raw), C.c_char_p(bytes(raw)))
            block_buffers[block] = buffer.value
        buffer = U(); fn('glCreateBuffers', None, I, C.POINTER(U))(1, C.byref(buffer))
        fn('glNamedBufferData', None, U, C.c_ssize_t, P, U)(buffer, 16384, (C.c_ubyte * 16384)(), 0x88E4)
        for slot in (30, 31): fn('glBindBufferBase', None, U, U, U)(0x90D2, slot, buffer)
        original_units = {}
        for program, uniforms in zip(programs, reflection):
            unit = 64 if program == programs[0] else 0
            for name, data in uniforms.items():
                if data['block'] >= 0: continue
                location = data['location']
                if data['type'] == 0x8B52:
                    values = [0.] * (data['size'] * 4)
                    if name == 'g_transforms[0]':
                        for row in (0, 1, 2, 3, 4, 5, 6, 7, 8): values[row * 4 + row % 3] = 1
                        values[39] = 1
                    else: values[0] = 1
                    fn('glProgramUniform4fv', None, U, I, I, P)(program, location, data['size'], (C.c_float * len(values))(*values))
                    if program == programs[0]: uniform_note(program, location, data['size'], (C.c_float * len(values))(*values))
                else:
                    for i in range(data['size']):
                        fn('glProgramUniform1i', None, U, I, I)(program, location + i, unit)
                        tex = opaque if args.npr_program and name == '_perf_rs_npr_16' else textures[data['type']][0]
                        fn('glBindTextureUnit', None, U, U)(unit, tex)
                        if program == programs[0]:
                            sampler_note(program, location + i, unit)
                            original_units[name] = unit
                        unit += 1
        target = U(); fn('glCreateTextures', None, U, I, C.POINTER(U))(0xDE1, 1, C.byref(target))
        fn('glTextureStorage2D', None, U, I, U, I, I)(target, 1, 0x8814, 16, 16)
        fbo = U(); fn('glCreateFramebuffers', None, I, C.POINTER(U))(1, C.byref(fbo))
        fn('glNamedFramebufferTexture', None, U, U, U, I)(fbo, 0x8CE0, target, 0)
        fn('glBindFramebuffer', None, U, U)(0x8D40, fbo)
        if args.npr_program:
            for attachment in (1, 2):
                extra = U(); fn('glCreateTextures', None, U, I, C.POINTER(U))(0xDE1, 1, C.byref(extra))
                fn('glTextureStorage2D', None, U, I, U, I, I)(extra, 1, 0x8814, 16, 16)
                fn('glNamedFramebufferTexture', None, U, U, U, I)(fbo, 0x8CE0 + attachment, extra, 0)
            fn('glDrawBuffers', None, I, P)(3, (U * 3)(0x8CE0, 0x8CE1, 0x8CE2))
        assert fn('glCheckFramebufferStatus', U, U)(0x8D40) == 0x8CD5
        vao = U(); fn('glCreateVertexArrays', None, I, C.POINTER(U))(1, C.byref(vao))
        fn('glBindVertexArray', None, U)(vao)
        vertex = U(); fn('glCreateBuffers', None, I, C.POINTER(U))(1, C.byref(vertex))
        positions = (C.c_float * 12)(-1, -1, 0, 1, 3, -1, 0, 1, -1, 3, 0, 1)
        fn('glNamedBufferData', None, U, C.c_ssize_t, P, U)(vertex, C.sizeof(positions), positions, 0x88E4)
        fn('glVertexArrayVertexBuffer', None, U, U, U, C.c_ssize_t, I)(vao, 0, vertex, 0, 16)
        fn('glVertexArrayAttribFormat', None, U, U, I, U, C.c_ubyte, U)(vao, 0, 4, 0x1406, 0, 0)
        fn('glEnableVertexArrayAttrib', None, U, U)(vao, 0)
        fn('glVertexAttrib3f', None, U, C.c_float, C.c_float, C.c_float)(1, 0, 0, 1)
        fn('glVertexAttrib4f', None, U, C.c_float, C.c_float, C.c_float, C.c_float)(2, 1, 0, 0, 1)
        fn('glViewport', None, I, I, I, I)(0, 0, 16, 16)
        check('inputs')
        pixels = []
        draw = fn('glDrawArrays', None, U, I, I)
        finish = fn('glFinish', None)
        for program in programs + [programs[0]]:
            fn('glUseProgram', None, U)(program)
            check('program')
            if len(pixels) == 2: assert integrated(program, 1) == 1
            else: draw(4, 0, 3)
            finish(); check('draw')
            image = (C.c_float * 1024)()
            fn('glReadPixels', None, I, I, I, I, U, U, P)(0, 0, 16, 16, 0x1908, 0x1406, image)
            pixels.append(bytes(image))
            assert all(math.isfinite(x) for x in image)
            assert any(image[i] > 0 for i in range(len(image)) if i % 4 != 3)
        if not pixels[0] == pixels[1] == pixels[2]:
            arrays = [struct.unpack('<1024f', raw) for raw in pixels]
            raise AssertionError(dict(stage='initial pixels', samples=[a[:12] for a in arrays],
                max_delta=[max(abs(x-y) for x,y in zip(arrays[0], a)) for a in arrays[1:]],
                different=[sum(x!=y for x,y in zip(arrays[0], a)) for a in arrays[1:]]))
        report['identical_pixels'] = True
        update4 = SystemGL.api(wrapper, 'TestRegularUpdate4', None, U, I, I, P, I)
        try_begin = SystemGL.api(wrapper, 'TestRegularTry', I, U)
        drop_shadow = SystemGL.api(wrapper, 'TestRegularDropShadow', None, U)
        update_sampler = SystemGL.api(wrapper, 'TestRegularUpdateSampler', None, U, I, Q)
        def read_image():
            image = (C.c_float * 1024)()
            fn('glReadPixels', None, I, I, I, I, U, U, P)(0, 0, 16, 16, 0x1908, 0x1406, image)
            assert all(math.isfinite(x) for x in image)
            return bytes(image)
        def state():
            values = []
            for enum in (0x8B8D, 0x84E0, 0x85B5, 0x8CA6, 0x8CAA, 0x8A28):
                value = I(); fn('glGetIntegerv', None, U, C.POINTER(I))(enum, C.byref(value)); values.append(value.value)
            for unit in range(96):
                for enum in (0x8069, 0x806A, 0x8514, 0x8C1D, 0x8C2C, 0x8919):
                    value = I(); fn('glGetIntegeri_v', None, U, U, C.POINTER(I))(enum, unit, C.byref(value)); values.append(value.value)
            return values
        sentinel = U(); fn('glCreateSamplers', None, I, C.POINTER(U))(1, C.byref(sentinel))
        for parameter in (0x2801, 0x2800): fn('glSamplerParameteri', None, U, U, I)(sentinel, parameter, 0x2601)
        for unit in range(64, 96):
            for kind in (0x8B5E, 0x8B60, 0x8DD0, 0x8DC4):
                fn('glBindTextureUnit', None, U, U)(unit, textures[kind][0])
            fn('glBindSampler', None, U, U)(unit, sentinel)
        fn('glActiveTexture', None, U)(0x84C0 + 37)
        alternate, _ = texture(0xDE1, 0x8058)
        color = (C.c_float * 4)(0.7, 0.2, 0.6, 0.8)
        fn('glClearTexImage', None, U, I, U, U, P)(alternate, 0, 0x1908, 0x1406, color)
        alternate_handle = fn('glGetTextureSamplerHandleARB', Q, U, U)(alternate, sentinel)
        fn('glMakeTextureHandleResidentARB', None, Q)(alternate_handle)
        handle_note(alternate_handle, alternate, sentinel)
        perdraw = reflection[0]['g_per_draw[0]']['location']
        material = reflection[0]['_amdshim_map_handle_pairs[0]']
        material_base = bytes(block_data[material['block']])
        shadow_block = reflection[0]['g_plane_distances']['block']
        for cascade, depth in enumerate((0.25, 0.6, 0.8)):
            depth_texture, depth_handle = texture(0xDE1, 0x8CAC, True)
            value = C.c_float(depth)
            fn('glClearTexImage', None, U, I, U, U, P)(depth_texture, 0, 0x1902, 0x1406, C.byref(value))
            member = reflection[0][f'g_cascaded_shadow_map_handle_{cascade}']
            struct.pack_into('<Q', block_data[member['block']], member['offset'], depth_handle)
            for row, vector in enumerate(((0.2, 0, 0, 0.5), (0, 0.2, 0, 0.5), (0, 0, 0, 0.5))):
                set4('g_shadow_fetches[0]', vector, 3 * cascade + row)
        def upload_block(block):
            raw = block_data[block]
            fn('glNamedBufferSubData', None, U, C.c_ssize_t, C.c_ssize_t, P)(block_buffers[block], 0, len(raw), C.c_char_p(bytes(raw)))
            shadow_note(block_buffers[block], len(raw), C.c_char_p(bytes(raw)))
        hashes = set()
        for case in range(20):
            raw = block_data[material['block']]
            raw[:] = material_base
            struct.pack_into('<Q', raw, material['offset'] + 8 * (case % 19), alternate_handle)
            flag_member = reflection[0]['g_npr_eye_parami']
            struct.pack_into('<I', raw, flag_member['offset'], 64 | (128 if case % 2 else 0))
            upload_block(material['block'])
            set4('g_plane_distances', ((1.2, 2, 3, 0), (0.3, 1.5, 3, 0), (0.3, 0.6, 2, 0))[case % 3])
            upload_block(shadow_block)
            planar = reflection[0]['g_planar_reflection_map_handle']
            struct.pack_into('<Q', block_data[planar['block']], planar['offset'], alternate_handle if case % 2 else 0)
            upload_block(planar['block'])
            # The declared array has four vec4s; reflection exposes only two.
            transparency = struct.unpack('<f', struct.pack('<I', 2 if case % 4 else 0))[0]
            values = (C.c_float * 16)(0.1 + case * 0.03, 0, 0, 0, 1, 0, case * 0.01, transparency)
            update4(programs[0], perdraw, 4, values, case % 2)
            draw(4, 0, 3); expected = read_image()
            saved = state()
            assert integrated(programs[0], 1) == 1
            actual = read_image()
            if actual != expected:
                a, b = struct.unpack('<1024f', actual), struct.unpack('<1024f', expected)
                raise AssertionError(dict(case=case, max_delta=max(abs(x-y) for x,y in zip(a,b)),
                    actual=a[:8], expected=b[:8]))
            assert state() == saved, ('state', case)
            hashes.add(hashlib.sha256(expected).hexdigest())
            check(f'variation {case}')
        assert len(hashes) > 4
        report['material_uniform_cases'] = 20
        report['distinct_pixel_hashes'] = len(hashes)
        report['state_restored'] = True
        report['shader_paths'] = ['material slots', 'screen shadow', 'three cascades', 'water', 'planar reflection']
        if args.npr_program:
            saved_blocks = {block: bytes(raw) for block, raw in block_data.items()}
            def read_mrt():
                images = []
                for attachment in range(3):
                    fn('glReadBuffer', None, U)(0x8CE0 + attachment)
                    images.append(read_image())
                fn('glReadBuffer', None, U)(0x8CE0)
                return images
            def clear_mrt():
                for attachment in range(3):
                    fn('glClearBufferfv', None, U, I, P)(0x1800, attachment,
                        (C.c_float * 4)(0.03125, 0.0625, 0.125, 0.25))
            ramp, ramp_handle = texture(0xDE1, 0x8058, side=4)
            ramp_values = (C.c_float * 64)(*[0.1 + 0.2 * x if channel < 3 else 1
                for y in range(4) for x in range(4) for channel in range(4)])
            fn('glTextureSubImage2D', None, U, I, I, I, I, I, U, U, P)(ramp, 0, 0, 0, 4, 4, 0x1908, 0x1406, ramp_values)
            mask, mask_handle = texture(0xDE1, 0x8058, side=4)
            mask_values = (C.c_float * 64)(*[x / 3 for y in range(4) for x in range(4) for channel in range(4)])
            fn('glTextureSubImage2D', None, U, I, I, I, I, I, U, U, P)(mask, 0, 0, 0, 4, 4, 0x1908, 0x1406, mask_values)
            uv = U(); fn('glCreateBuffers', None, I, C.POINTER(U))(1, C.byref(uv))
            fn('glNamedBufferData', None, U, C.c_ssize_t, P, U)(uv, 24, (C.c_float * 6)(0, 0, 2, 0, 0, 2), 0x88E4)
            fn('glVertexArrayVertexBuffer', None, U, U, U, C.c_ssize_t, I)(vao, 3, uv, 0, 8)
            fn('glVertexArrayAttribFormat', None, U, U, I, U, C.c_ubyte, U)(vao, 3, 2, 0x1406, 0, 0)
            fn('glVertexArrayAttribBinding', None, U, U, U)(vao, 3, 3)
            fn('glEnableVertexArrayAttrib', None, U, U)(vao, 3)
            unique = reflection[0]['g_unique_shadow_map_handle']
            unique_handles = []
            for depth in (0.2, 0.8):
                depth_tex, depth_handle = texture(0xDE1, 0x8CAC, True)
                fn('glClearTexImage', None, U, I, U, U, P)(depth_tex, 0, 0x1902, 0x1406, C.byref(C.c_float(depth)))
                unique_handles.append(depth_handle)
            for row, vector in enumerate(((0.2, 0, 0, 0.5), (0, 0.2, 0, 0.5), (0, 0, 0, 0.5))):
                set4('g_unique_shadow_fetcher[0]', vector, row)
            for row in range(0, 20, 2):
                set4('g_texcoord_transforms[0]', (1, 0, 0, 0), row)
                set4('g_texcoord_transforms[0]', (0, 1, 0, 0), row + 1)
            upload_block(reflection[0]['g_texcoord_transforms[0]']['block'])
            struct.pack_into('<Q', block_data[material['block']], material['offset'] + 8 * 5, ramp_handle)
            struct.pack_into('<I', block_data[material['block']], reflection[0]['g_npr_eye_parami']['offset'], 64)
            fn('glBindTextureUnit', None, U, U)(original_units['g_cloud_shadow_sampler'], opaque)
            unique_images, masks = [], []
            for case in range(12):
                extra_flags = 4 if case < 8 else 5
                gain = 0 if case == 7 else 1
                struct.pack_into('<Q', block_data[unique['block']], unique['offset'], unique_handles[case % 2])
                struct.pack_into('<Q', block_data[material['block']], material['offset'] + 8 * 16,
                                 mask_handle if case in (4, 5, 6) else opaque_handle)
                set4('g_color_gains[0]', (1, 1, 1, gain))
                set4('g_color_gains[0]', (1, 1, 1, 0.8 if case == 6 else 0.5), 4)
                upload_block(unique['block']); upload_block(material['block'])
                values = (C.c_float * 16)(0.1, 0, 0, 0, 1, 0, (-1, 0.2, 0.8, 2)[case % 4],
                    struct.unpack('<f', struct.pack('<I', extra_flags))[0], 0.2, 0.3, 0.4, 1, 0, 0, 0, 0)
                update4(programs[0], perdraw, 4, values, case % 2)
                clear_mrt(); draw(4, 0, 3); expected = read_mrt(); saved = state()
                clear_mrt(); assert integrated(programs[0], 1) == 1
                assert read_mrt() == expected and state() == saved, ('NPR MRT', case)
                unique_images.append(hashlib.sha256(expected[0]).hexdigest())
                masks.append(sum(struct.unpack_from('<f', expected[0], pixel * 16 + 12)[0] != 0.25 for pixel in range(256)))
            assert unique_images[0] != unique_images[1], 'unique shadow handle had no visible effect'
            if args.npr_program == 1748:
                assert 0 in masks and 256 in masks and any(0 < count < 256 for count in masks), masks
                assert all(0 < masks[case] < 256 for case in (4, 5, 6)), ('opacity coverage', masks)
            report['npr_mrt_cases'] = dict(cases=12, attachments=3, coverage=masks,
                distinct_images=len(set(unique_images)), unique_shadow_changes_output=True)
            report['shader_paths'] = ['fixed material slots', 'three cascades', 'unique shadow', 'NPR tone', 'opacity', 'dissolve', 'three MRT outputs']
            for block, raw in saved_blocks.items():
                block_data[block][:] = raw; upload_block(block)
            fn('glDisableVertexArrayAttrib', None, U, U)(vao, 3)
            fn('glBindTextureUnit', None, U, U)(original_units['g_cloud_shadow_sampler'], textures[0x8B5E][0])
            update4(programs[0], perdraw, 4, (C.c_float * 16)(0.67, 0, 0, 0, 1, 0, 0.19,
                struct.unpack('<f', struct.pack('<I', 2))[0]), 0)
        allocate = SystemGL.api(wrapper, 'TestRegularAllocate', None, U, C.c_ssize_t)
        upload = SystemGL.api(wrapper, 'TestRegularUpload', None, U, C.c_ssize_t, C.c_ssize_t, P)
        uploaded_bytes = SystemGL.api(wrapper, 'TestRegularUploadBytes', I, U, C.c_ssize_t, C.c_ssize_t, P, I)
        reflected_ranges = (I * (33 * 3))()
        range_count = SystemGL.api(wrapper, 'TestRegularRanges', I, U, P)(programs[0], reflected_ranges)
        range_rows = [list(reflected_ranges[i * 3:i * 3 + 3]) for i in range(range_count)]
        assert 0 < range_count <= 33
        if args.npr_program:
            shadow_block = reflection[0]['g_unique_shadow_map_handle']['block']
            assert [row[1:] for row in range_rows if row[0] == shadow_block] == [[224, 272], [784, 792]]
        report['reflected_read_ranges'] = range_rows
        material_buffer = U(); fn('glCreateBuffers', None, I, C.POINTER(U))(1, C.byref(material_buffer))
        allocate(material_buffer, 3840)
        material_raw = bytes(block_data[material['block']][:712])
        material_offset = 1536
        fn('glBindBufferRange', None, U, U, U, C.c_ssize_t, C.c_ssize_t)(0x8A11, 3, material_buffer, material_offset, 720)
        saved = state(); assert try_begin(programs[0]) == 0; assert state() == saved
        hole = material['offset'] + 1
        upload(material_buffer, material_offset, hole, C.c_char_p(material_raw[:hole]))
        assert try_begin(programs[0]) == 0
        upload(material_buffer, material_offset + hole + 1, 711 - hole, C.c_char_p(material_raw[hole + 1:]))
        assert try_begin(programs[0]) == 0, 'one-byte handle hole must remain unknown'
        failure = (Q * 7)()
        failure_state = SystemGL.api(wrapper, 'TestRegularFailure', None, U, P)
        failure_state(programs[0], failure)
        assert failure[0] > 0 and failure[1] == material_buffer.value and failure[2] == 3
        assert failure[3] <= material_offset + hole < failure[3] + failure[4]
        assert (failure[5], failure[6]) == (0, 0)
        upload(material_buffer, material_offset + hole, 1, C.c_char_p(material_raw[hole:hole + 1]))
        readback = C.create_string_buffer(712)
        assert uploaded_bytes(material_buffer, material_offset, 712, readback, 1) == 0, 'old route unexpectedly covers null allocation'
        assert uploaded_bytes(material_buffer, material_offset, 712, readback, 0) == 1
        assert readback.raw == material_raw
        draw(4, 0, 3); expected = read_image(); saved = state()
        assert integrated(programs[0], 1) == 1
        assert read_image() == expected and state() == saved
        clear_bytes = SystemGL.api(wrapper, 'TestRegularClearBytes', None, U, C.c_ssize_t, C.c_ssize_t)
        clear_bytes(material_buffer, material_offset + 11, 3)
        assert try_begin(programs[0]) == 1, 'unread prefix validity must not reject handles'
        assert uploaded_bytes(material_buffer, material_offset, 11, readback, 0) == 1
        assert uploaded_bytes(material_buffer, material_offset + 11, 3, readback, 0) == 0
        draw(4, 0, 3); expected = read_image(); saved = state()
        assert integrated(programs[0], 1) == 1
        assert read_image() == expected and state() == saved
        upload(material_buffer, material_offset + 11, 3, C.c_char_p(material_raw[11:14]))
        assert try_begin(programs[0]) == 1
        clear_bytes(material_buffer, material_offset + hole, 1)
        assert try_begin(programs[0]) == 0
        upload(material_buffer, material_offset + hole, 1, C.c_char_p(material_raw[hole:hole + 1]))
        assert try_begin(programs[0]) == 1
        SystemGL.api(wrapper, 'TestRegularCopyBytes', None, U, U, C.c_ssize_t, C.c_ssize_t)(
            block_buffers[material['block']], material_buffer, material_offset + 100, 4)
        assert try_begin(programs[0]) == 1, 'copy outside handles must not reject'
        upload(material_buffer, material_offset + 100, 4, C.c_char_p(material_raw[100:104]))
        assert try_begin(programs[0]) == 1
        SystemGL.api(wrapper, 'TestRegularCopyBytes', None, U, U, C.c_ssize_t, C.c_ssize_t)(
            block_buffers[material['block']], material_buffer, material_offset + hole, 1)
        assert try_begin(programs[0]) == 0
        upload(material_buffer, material_offset + hole, 1, C.c_char_p(material_raw[hole:hole + 1]))
        assert try_begin(programs[0]) == 1
        SystemGL.api(wrapper, 'TestRegularMapBytes', None, U)(material_buffer)
        assert try_begin(programs[0]) == 0
        upload(material_buffer, material_offset, 712, C.c_char_p(material_raw))
        assert try_begin(programs[0]) == 1
        allocate(material_buffer, 3840)
        assert try_begin(programs[0]) == 0
        upload(material_buffer, material_offset, 712, C.c_char_p(material_raw))
        assert try_begin(programs[0]) == 1
        SystemGL.api(wrapper, 'TestRegularGPUBuffer', None, U)(material_buffer)
        upload(material_buffer, material_offset, 712, C.c_char_p(material_raw))
        assert try_begin(programs[0]) == 0, 'GPU-exposed buffer must stay ineligible'
        failure_state(programs[0], failure)
        assert failure[1] == material_buffer.value and (failure[5], failure[6]) == (0, 1)
        table = SystemGL.api(wrapper, 'TestRegularProgramTable', P)()
        failure_snapshot = read_sampler_failures(C.string_at, table)
        row = failure_snapshot[str(programs[0])]
        assert row['count'] == failure[0] and row['buffer'] == material_buffer.value
        assert row['gpu_exposed'] and not row['mapped'] and row['original_program'] == programs[0]
        assert sampler_failure_delta(failure_snapshot, failure_snapshot) == {}
        assert sampler_failure_delta({}, failure_snapshot)[str(programs[0])]['delta'] == failure[0]
        report['external_failure_metadata_verified'] = True
        SystemGL.api(wrapper, 'TestRegularDeleteBuffer', None, U)(material_buffer)
        assert uploaded_bytes(material_buffer, material_offset, 712, readback, 0) == 0
        # The compatibility context permits reuse of a deleted buffer name.
        # New storage must not inherit the old object's GPU-write exposure.
        fn('glBindBuffer', None, U, U)(0x8F37, material_buffer)
        check('recreate deleted material buffer name')
        allocate(material_buffer, 3840)
        assert uploaded_bytes(material_buffer, material_offset, 712, readback, 0) == 0
        upload(material_buffer, material_offset, 712, C.c_char_p(material_raw))
        assert uploaded_bytes(material_buffer, material_offset, 712, readback, 0) == 1, \
            'new buffer name lifetime must not inherit old GPU exposure'
        assert readback.raw[:712] == material_raw
        fn('glBindBufferRange', None, U, U, U, C.c_ssize_t, C.c_ssize_t)(
            0x8A11, 3, material_buffer, material_offset, 720)
        assert try_begin(programs[0]) == 1
        draw(4, 0, 3); expected = read_image(); saved = state()
        assert integrated(programs[0], 1) == 1
        assert read_image() == expected and state() == saved
        SystemGL.api(wrapper, 'TestRegularGPUBuffer', None, U)(material_buffer)
        allocate(material_buffer, 3840)
        upload(material_buffer, material_offset, 712, C.c_char_p(material_raw))
        assert try_begin(programs[0]) == 0, 'reallocation of a live GPU-exposed object stays excluded'
        SystemGL.api(wrapper, 'TestRegularDeleteBuffer', None, U)(material_buffer)
        fn('glBindBuffer', None, U, U)(0x8F37, 0)
        report['deleted_name_reuse'] = dict(new_upload_eligible=True,
            pixels_and_state_equal=True, live_gpu_reallocation_excluded=True)
        fn('glBindBufferBase', None, U, U, U)(0x8A11, 3, block_buffers[material['block']])
        assert try_begin(programs[0]) == 1
        report['production_upload_cases'] = ['null allocation', 'nonzero range', 'split uploads',
            'one-byte handle hole', 'legacy route misses', 'identical pixels/state',
            'unread prefix clear/copy stays eligible', 'handle clear/copy rejects',
            'failure metadata', 'mapping', 'reallocation', 'GPU use', 'deletion']
        if args.npr_program:
            shadow_buffer = block_buffers[shadow_block]
            shadow_raw = bytes(block_data[shadow_block])
            SystemGL.api(wrapper, 'TestRegularDropShadow', None, U)(shadow_buffer)
            for first, end in ((224, 272), (784, 792)):
                upload(shadow_buffer, first, end - first, C.c_char_p(shadow_raw[first:end]))
            assert try_begin(programs[0]) == 1, 'unknown local-shadow prefix/gap is not a sampled handle'
            assert uploaded_bytes(shadow_buffer, 272, 512, C.create_string_buffer(512), 0) == 0
            draw(4, 0, 3); expected = read_image(); saved = state()
            assert integrated(programs[0], 1) == 1
            assert read_image() == expected and state() == saved
            clear_bytes(shadow_buffer, 787, 1)
            assert try_begin(programs[0]) == 0, 'unique-shadow handle hole must reject'
            failure_state(programs[0], failure)
            assert (failure[1], failure[2], failure[3], failure[4]) == (shadow_buffer, 2, 784, 8)
            upload(shadow_buffer, 787, 1, C.c_char_p(shadow_raw[787:788]))
            assert try_begin(programs[0]) == 1
            shadow_note(shadow_buffer, len(shadow_raw), C.c_char_p(shadow_raw))
            report['sparse_shadow_upload'] = dict(known_bytes=56, unknown_gap_bytes=512,
                identical_pixels_and_state=True, required_handle_hole_rejects=True)
        names = (U * 640)()
        fn('glCreateBuffers', None, I, C.POINTER(U))(len(names), names)
        groups = {}
        for name in names:
            if name == material_buffer.value:
                continue
            groups.setdefault(name % 128, []).append(name)
        collision = next(group[:5] for group in groups.values() if len(group) >= 5)
        for name in collision:
            allocate(name, 64)
            upload(name, 0, 64, C.c_char_p(bytes([name % 251]) * 64))
            initial = C.create_string_buffer(64)
            assert uploaded_bytes(name, 0, 64, initial, 0) == 1, ('fresh upload not admitted', name)
        probe = C.create_string_buffer(64)
        for name in collision:
            assert uploaded_bytes(name, 0, 64, probe, 0) == 1, ('live upload lost to bucket conflict', name)
            assert probe.raw == bytes([name % 251]) * 64
        fn('glDeleteBuffers', None, I, C.POINTER(U))(len(names), names)
        report['colliding_buffer_uploads_retained'] = len(collision)
        check('production uploads')
        persistent_tail = SystemGL.api(wrapper, 'TestRegularPersistentTail', P,
            U, C.c_ssize_t, P, C.c_ssize_t, C.c_ssize_t)
        alignment = I()
        fn('glGetIntegerv', None, U, C.POINTER(I))(0x8A34, C.byref(alignment))
        binding_offset = max(256, alignment.value)
        mapped = {}
        fields = [('g_reflection_proxy_count', 4), ('g_water_normal_map_handle', 8),
            ('g_planar_reflection_map_handle', 8), ('g_screen_shadow_map_handle', 8),
            ('g_ssao_map_handle', 8), ('g_local_shadow_map_handle', 8)]
        fields += [(f'g_cascaded_shadow_map_handle_{i}', 8) for i in range(3)]
        if args.npr_program:
            fields.append(('g_unique_shadow_map_handle', 8))
        spans = {}
        for name, size in fields:
            member = reflection[0][name]
            spans.setdefault(member['block'], []).append((member['offset'], member['offset'] + size))
        for block, ranges in spans.items():
            first, end = min(a for a, b in ranges), max(b for a, b in ranges)
            raw = block_data[block]
            storage = bytes(binding_offset) + bytes(raw)
            name = U(); fn('glCreateBuffers', None, I, C.POINTER(U))(1, C.byref(name))
            pointer = persistent_tail(name, len(storage), C.c_char_p(storage), binding_offset + first, end - first)
            assert pointer, ('persistent map', block)
            binding = I()
            fn('glGetActiveUniformBlockiv', None, U, U, U, C.POINTER(I))(programs[0], block, 0x8A3F, C.byref(binding))
            fn('glBindBufferRange', None, U, U, U, C.c_ssize_t, C.c_ssize_t)(
                0x8A11, binding.value, name, binding_offset, len(raw))
            mapped[block] = dict(buffer=name.value, pointer=pointer, first=first,
                end=end, binding=binding.value, raw=bytearray(raw))
        check('persistent tails')
        assert try_begin(programs[0]) == 1, 'only sampler tails are CPU-mapped'
        mapped_hashes = set()
        changing = reflection[0]['g_ssao_map_handle' if args.npr_program else 'g_planar_reflection_map_handle']
        scene = mapped[changing['block']]
        for case in range(4):
            original_handle = textures[0x8B5E][1] if args.npr_program else 0
            struct.pack_into('<Q', scene['raw'], changing['offset'], alternate_handle if case % 2 else original_handle)
            C.memmove(scene['pointer'], bytes(scene['raw'][scene['first']:scene['end']]), scene['end'] - scene['first'])
            draw(4, 0, 3); expected = read_image(); saved = state()
            assert integrated(programs[0], 1) == 1
            assert read_image() == expected and state() == saved, ('fresh mapped tails', case)
            mapped_hashes.add(hashlib.sha256(expected).hexdigest())
        assert len(mapped_hashes) == 2, 'mapped handle change did not affect output'
        proxy = reflection[0]['g_reflection_proxy_count']
        proxy_map = mapped[proxy['block']]
        proxy_pointer = proxy_map['pointer'] + proxy['offset'] - proxy_map['first']
        C.memmove(proxy_pointer, struct.pack('<f', 1), 4)
        saved = state(); assert try_begin(programs[0]) == 0; assert state() == saved
        C.memmove(proxy_pointer, struct.pack('<f', 0), 4)
        assert try_begin(programs[0]) == 1
        report['persistent_mapped_tails'] = dict(binding_offset=binding_offset,
            spans={block: [m['first'], m['end']] for block, m in mapped.items()},
            distinct_pixel_hashes=len(mapped_hashes), changing_proxy_falls_back=True)
        for block, m in mapped.items():
            SystemGL.api(wrapper, 'TestRegularUnmap', None, U)(m['buffer'])
            SystemGL.api(wrapper, 'TestRegularDeleteBuffer', None, U)(m['buffer'])
            fn('glBindBufferBase', None, U, U, U)(0x8A11, m['binding'], block_buffers[block])
        check('persistent tail retirement')
        update_sampler(programs[0], reflection[0]['g_cloud_shadow_sampler']['location'], alternate_handle)
        draw(4, 0, 3); expected = read_image(); saved = state()
        assert integrated(programs[0], 1) == 0
        assert read_image() == expected and state() == saved
        SystemGL.api(wrapper, 'TestRegularUpdateUnit', None, U, I, I)(programs[0], reflection[0]['g_cloud_shadow_sampler']['location'], original_units['g_cloud_shadow_sampler'])
        assert try_begin(programs[0]) == 1
        report['default_handle_mode_uses_original'] = True
        scene = reflection[0]['g_reflection_proxy_count']
        scene_raw = block_data[scene['block']]
        original_scene = bytes(scene_raw)
        for value in (1., float('nan')):
            struct.pack_into('<f', scene_raw, scene['offset'], value)
            shadow_note(block_buffers[scene['block']], len(scene_raw), C.c_char_p(bytes(scene_raw)))
            saved = state(); assert try_begin(programs[0]) == 0; assert state() == saved
        scene_raw[:] = original_scene
        shadow_note(block_buffers[scene['block']], len(scene_raw), C.c_char_p(bytes(scene_raw)))
        drop_shadow(block_buffers[material['block']])
        saved = state(); assert try_begin(programs[0]) == 0; assert state() == saved
        upload_block(material['block'])
        assert try_begin(programs[0]) == 1
        deleted_texture, deleted_handle = texture(0xDE1, 0x8058)
        raw = block_data[material['block']]
        saved_material = bytes(raw)
        struct.pack_into('<Q', raw, material['offset'], deleted_handle)
        shadow_note(block_buffers[material['block']], len(raw), C.c_char_p(bytes(raw)))
        assert try_begin(programs[0]) == 1
        SystemGL.api(wrapper, 'TestRegularDeleteTexture', None, U)(deleted_texture)
        assert fn('glIsTexture', C.c_ubyte, U)(deleted_texture) == 0
        assert try_begin(programs[0]) == 0
        raw[:] = saved_material
        shadow_note(block_buffers[material['block']], len(raw), C.c_char_p(bytes(raw)))
        report['fallback_preserves_state'] = True
        if hasattr(wrapper, 'TestRegularBindingProc'):
            binding_proc = SystemGL.api(wrapper, 'TestRegularBindingProc', P, C.c_char_p)
            def hook(name, *params):
                address = binding_proc(name.encode())
                assert address, name
                return C.WINFUNCTYPE(None, *params)(address)
            bind_unit = hook('glBindTextureUnit', U, U)
            bind_many = hook('glBindTextures', U, I, P)
            bind_multi = hook('glBindMultiTextureEXT', U, U, U)
            bind_sampler = hook('glBindSampler', U, U)
            bind_samplers = hook('glBindSamplers', U, I, P)
            active_unit = hook('glActiveTexture', U)
            bind_legacy = hook('glBindTexture', U, U)
            target_bind = SystemGL.api(wrapper, 'TestRegularTargetBind', I, I)
            saved_units = []
            unit_targets = ((0x8069, 0xDE1), (0x806A, 0x806F), (0x8514, 0x8513),
                            (0x8C1D, 0x8C1A), (0x8C2C, 0x8C2A))
            for unit in range(63, 97):
                for binding, target in unit_targets + ((0x8919, 0),):
                    value = I()
                    fn('glGetIntegeri_v', None, U, U, P)(binding, unit, C.byref(value))
                    saved_units.append((unit, target, value.value))
            report['private_unit_mutations'] = []
            for direct in (1, 0):
                target_bind(direct)
                seed_state(programs[0])
                assert integrated_raw(programs[0], 1) == 1
                test_tex, test_sampler = U(), U()
                fn('glCreateTextures', None, U, I, P)(0xDE1, 1, C.byref(test_tex))
                fn('glCreateSamplers', None, I, P)(1, C.byref(test_sampler))
                def verify_mutation(label, mutate):
                    print('private unit mutation', direct, label, flush=True)
                    mutate()
                    expected_state = state()
                    # Do not reseed: invalidation must come from the real hooks.
                    assert integrated_raw(programs[0], 2) == 2
                    assert state() == expected_state, (direct, label)
                    check(label)
                    report['private_unit_mutations'].append([direct, label])
                verify_mutation('texture unit', lambda: [bind_unit(u, test_tex) for u in range(64, 96)])
                verify_mutation('clear all targets', lambda: [bind_unit(u, 0) for u in range(64, 96)])
                verify_mutation('multi texture range', lambda: bind_many(63, 34, (U * 34)(*[test_tex.value] * 34)))
                verify_mutation('null texture range', lambda: bind_many(63, 34, None))
                verify_mutation('explicit cube target', lambda: [bind_multi(0x84C0 + u, 0x8513, textures[0x8B60][0]) for u in range(64, 96)])
                verify_mutation('legacy texture', lambda: [(active_unit(0x84C0 + u), bind_legacy(0xDE1, test_tex)) for u in range(64, 96)])
                verify_mutation('sampler unit', lambda: [bind_sampler(u, test_sampler) for u in range(64, 96)])
                verify_mutation('null sampler range', lambda: bind_samplers(63, 34, None))
                verify_mutation('sampler range', lambda: bind_samplers(63, 34, (U * 34)(*[test_sampler.value] * 34)))
                fn('glPushAttrib', None, U)(0x40000)  # GL_TEXTURE_BIT
                verify_mutation('inside attribute stack', lambda: [bind_unit(u, 0) for u in range(64, 96)])
                verify_mutation('pop texture attributes', hook('glPopAttrib'))
                verify_mutation('delete bound texture', lambda: hook('glDeleteTextures', I, P)(1, C.byref(test_tex)))
                verify_mutation('delete bound sampler', lambda: hook('glDeleteSamplers', I, P)(1, C.byref(test_sampler)))
                # Restore fixture sampler bindings before the lifetime tests.
                for u in range(64, 96): bind_sampler(u, sentinel)
                disable_cache()
            target_bind(1)
            for unit, target, name in saved_units:
                if target: bind_multi(0x84C0 + unit, target, name)
                else: bind_sampler(unit, name)
            active_unit(0x84C0 + 37)
        if args.before:
            previous = C.WinDLL(str(args.before.resolve()))
            previous_api = lambda name, result, *params: SystemGL.api(previous, name, result, *params)
            assert previous_api('TestRegularInstall', I, P, P, U, C.c_char_p, C.c_char_p)(
                gl.gl._handle, gl.get_proc, programs[0], sources[0][0], sources[1][0])
            for handle, (tex, sampler_name) in known_handles.items():
                if fn('glIsTexture', C.c_ubyte, U)(tex):
                    previous_api('TestRegularHandle', None, Q, U, U)(handle, tex, sampler_name)
            for block, raw in block_data.items():
                previous_api('TestRegularShadow', None, U, C.c_ssize_t, P)(
                    block_buffers[block], len(raw), C.c_char_p(bytes(raw)))
            for name, unit in original_units.items():
                previous_api('TestRegularSampler', None, U, I, I)(programs[0], reflection[0][name]['location'], unit)
            old_run = previous_api('TestRegularDraw', I, U, U)
            assert old_run(programs[0], 1) == 1  # Initialize the lazy GL API first.
            old_raw = old_run
            old_seed = previous_api('TestRegularSeedState', None, U)
            old_disable = previous_api('TestRegularDisableCache', None)
            def old_run(program, count):
                old_seed(program)
                result = old_raw(program, count)
                old_disable()
                return result
            saved = state()
            draw(4, 0, 3); expected = read_image()
            assert old_run(programs[0], 1) == 1
            assert read_image() == expected and state() == saved
            assert integrated(programs[0], 1) == 1
            assert read_image() == expected and state() == saved
            timings = {'before': [], 'after': []}
            fn('glEnable', None, U)(0x8C89)
            for run in (old_run, integrated): assert run(programs[0], 2000) == 2000
            finish()
            for repeat in range(8):
                order = (('before', old_run), ('after', integrated))
                if repeat % 2: order = tuple(reversed(order))
                for name, run in order:
                    start = time.perf_counter()
                    assert run(programs[0], 20000) == 20000
                    finish()
                    timings[name].append((time.perf_counter() - start) * 1000)
            fn('glDisable', None, U)(0x8C89)
            report['preparation_ms_20000_draws'] = timings
            report['preparation_median_ms'] = {k: statistics.median(v) for k, v in timings.items()}
            seed_state(programs[0])
            assert integrated_raw(programs[0], 1) == 1
            counters = (Q * 10)()
            assert SystemGL.api(wrapper, 'TestRegularMeasure', I, U, U, P)(programs[0], 1, counters) == 1
            report['warm_calls'] = dict(zip(('get', 'get_i', 'get_i64', 'get_tex', 'active',
                'bind_tex', 'bind_sampler', 'use', 'u1', 'bind_multi_tex'), counters))
            disable_cache()
            old_seed(programs[0])
            assert old_raw(programs[0], 1) == 1
            assert previous_api('TestRegularMeasure', I, U, U, P)(programs[0], 1, counters) == 1
            report['before_warm_calls'] = dict(zip(report['warm_calls'], counters))
            old_disable()
            check('previous preparation comparison')
        runner = C.WinDLL(str((args.runner or ROOT / 'build/staged-upload-20260911/residency_benchmark.dll').resolve()))
        run = SystemGL.api(runner, 'TestResidentDraws', C.c_double, P, U)
        extras = [texture(0xDE1, 0x8058) for _ in range(4096)]
        report['measurements'] = []
        for index, program in enumerate(programs[:1]):
            fn('glUseProgram', None, U)(program)
            run(draw, 32); finish()
            start = time.perf_counter(); submit = run(draw, 2000); finish()
            report['measurements'].append(dict(lowered=bool(index), submit_ms=submit, completed_ms=(time.perf_counter() - start) * 1000))
        fn('glUseProgram', None, U)(programs[0])
        start = time.perf_counter(); hits = integrated(programs[0], 2000); finish()
        report['measurements'].append(dict(integrated=True, hits=hits, completed_ms=(time.perf_counter() - start) * 1000))
        assert hits == 2000
        report['ordinary_samplers'] = SystemGL.api(wrapper, 'TestRegularSlots', I, U)(programs[0])
        delete_sampler = SystemGL.api(wrapper, 'TestRegularDeleteSampler', None, U)
        delete_sampler(0)
        assert try_begin(programs[0]) == 1
        lost_texture, lost_handle = texture(0xDE1, 0x8058)
        # The handle table is bounded; explicitly check unknown-handle fallback.
        raw = block_data[material['block']]
        saved_material = bytes(raw)
        struct.pack_into('<Q', raw, material['offset'], lost_handle)
        shadow_note(block_buffers[material['block']], len(raw), C.c_char_p(bytes(raw)))
        assert try_begin(programs[0]) == 0
        raw[:] = saved_material
        shadow_note(block_buffers[material['block']], len(raw), C.c_char_p(bytes(raw)))
        assert try_begin(programs[0]) == 1
        delete_sampler(sentinel)
        assert try_begin(programs[0]) == 0
        retire = SystemGL.api(wrapper, 'TestRegularRetire', None, U)
        retire(programs[0]); assert try_begin(programs[0]) == 0
        assert install(gl.gl._handle, gl.get_proc, programs[0], sources[0][0], sources[1][0]) == 1
        # Retiring a context also discards pending programs before any build.
        SystemGL.api(wrapper, 'TestRegularContextChange', None)()
        assert try_begin(programs[0]) == 0
        report['lifetime_fallbacks'] = ['table_capacity', 'texture_deletion', 'sampler_deletion', 'relink_notification', 'context_change']
        print(json.dumps(report['measurements']), flush=True)
        check('benchmark')
        (OUT / 'driver-test.json').write_text(json.dumps(report, indent=2))


if __name__ == '__main__': main()
