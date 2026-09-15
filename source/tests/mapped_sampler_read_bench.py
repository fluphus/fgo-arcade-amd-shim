"""Compare current sampler reads with fresh streaming loads on driver mappings."""
import argparse
import ctypes as C
import json
from pathlib import Path
import statistics

from battle_window_driver_reflection import SystemGL

ap = argparse.ArgumentParser()
ap.add_argument("--wrapper", type=Path, required=True)
ap.add_argument("--output", type=Path, required=True)
ap.add_argument("--before", type=Path, help="Compare against an earlier wrapper with streaming/coalescing enabled.")
args = ap.parse_args()
wrapper = C.WinDLL(str(args.wrapper.resolve()))
U, S, P = C.c_uint, C.c_size_t, C.c_void_p
bench = SystemGL.api(wrapper, "TestMappedSamplerRead", C.c_double,
                     P, S, S, S, U, C.c_int, C.POINTER(C.c_uint64))
equal = SystemGL.api(wrapper, "TestMappedSamplerEqual", C.c_int, P, S, S, S)
expected = SystemGL.api(wrapper, "TestMappedSamplerExpected", C.c_int, P, S, S, S, P)
protect = SystemGL.api(wrapper, "TestMappedMemoryProtect", U, P)
layout_read = SystemGL.api(wrapper, "TestMappedSamplerLayout", C.c_double,
                          P, S, P, P, U, U, C.c_int, P,
                          C.POINTER(C.c_uint64), C.POINTER(U))
before_wrapper = C.WinDLL(str(args.before.resolve())) if args.before else None
before_bench = SystemGL.api(before_wrapper, "TestMappedSamplerRead", C.c_double,
                            P, S, S, S, U, C.c_int, C.POINTER(C.c_uint64)) if before_wrapper else bench
before_layout = SystemGL.api(before_wrapper, "TestMappedSamplerLayout", C.c_double,
                             P, S, P, P, U, U, C.c_int, P,
                             C.POINTER(C.c_uint64), C.POINTER(U)) if before_wrapper else layout_read
layouts = {
    "469_514_view": ((224, 8), (256, 16)),
    "1156_scene": ((820, 4), (832, 8), (856, 8)),
    "469_sparse_material": ((592, 8), (624, 8), (688, 8), (704, 8)),
    "1156_dense_material": ((560, 152),),
    "469_proxy": ((820, 4),),
}


def read_layout(ptr, backing, fields, data, repeats, mode, reader=layout_read):
    offsets = (U * len(fields))(*(field[0] for field in fields))
    sizes = (U * len(fields))(*(field[1] for field in fields))
    checksum, merged = C.c_uint64(), U()
    elapsed = reader(ptr, backing, offsets, sizes, len(fields), repeats,
                     mode, data, C.byref(checksum), C.byref(merged))
    assert elapsed >= 0, (fields, elapsed)
    return elapsed, checksum.value, merged.value


results = []
layout_results = []
edge_cases = 0
gpu_cases = 0
layout_fresh_cases = 0
layout_edge_cases = 0
layout_gpu_cases = 0
equality_cases = 0
with SystemGL() as gl:
    renderer = gl.proc("glGetString", C.c_char_p, U)(0x1F01).decode()
    new = gl.proc("glCreateBuffers", None, C.c_int, P)
    storage = gl.proc("glNamedBufferStorage", None, U, S, P, U)
    map_range = gl.proc("glMapNamedBufferRange", P, U, S, S, U)
    flush = gl.proc("glFlushMappedNamedBufferRange", None, U, S, S)
    unmap = gl.proc("glUnmapNamedBuffer", C.c_ubyte, U)
    delete = gl.proc("glDeleteBuffers", None, C.c_int, P)
    error = gl.proc("glGetError", U)
    gpu_copy = gl.proc("glCopyNamedBufferSubData", None, U, U, S, S, S)
    barrier = gl.proc("glMemoryBarrier", None, U)
    finish = gl.proc("glFinish", None)
    for flags, access in ((0x42, 0x52), (0xC2, 0xC2)):
        buffer = U()
        new(1, C.byref(buffer))
        storage(buffer, 4096, None, flags)
        ptr = map_range(buffer, 0, 4096, access)
        assert ptr and error() == 0
        for generation in range(3):
            data = bytes((i * 7 + generation * 31) % 256 for i in range(4096))
            C.memmove(ptr, data, 4096)
            if access & 0x10:
                flush(buffer, 0, 4096)
            for offset in (0, 1, 4, 8, 15, 16, 3992):
                for size in (4, 8, 16, 24, 48, 64, 80, 128, 152):
                    if offset + size > 4096:
                        continue
                    assert expected(ptr, 4096, offset, size, data[offset:offset + size])
                    assert equal(ptr, 4096, offset, size), (access, generation, offset, size)
                    equality_cases += 1
            for shift, backing in ((1, 4094), (8, 4087), (0, 4093)):
                for size in (4, 8, 16, 24):
                    for offset in (0, 3, backing - size):
                        assert expected(ptr + shift, backing, offset, size,
                                        data[shift + offset:shift + offset + size])
                        edge_cases += 1
            for fields in layouts.values():
                for mode in (0, 1):
                    read_layout(ptr, 4096, fields, data, 1, mode)
                    layout_fresh_cases += 1
            for shift, backing, fields in (
                (1, 4094, ((0, 8), (32, 16))),
                (8, 4087, ((0, 8), (32, 16))),
                (0, 4093, ((4045, 8), (4077, 16))),
                (1, 4094, ((820, 4), (832, 8), (856, 8))),
            ):
                read_layout(ptr + shift, backing, fields,
                            data[shift:shift + backing], 1, 1)
                layout_edge_cases += 1
        for name, fields in layouts.items():
            times = {0: [], 1: []}
            sums = set()
            merged_counts = set()
            for repeat in range(3):
                for mode in ((0, 1) if repeat % 2 == 0 else (1, 0)):
                    elapsed, checksum, merged = read_layout(
                        ptr, 4096, fields, data, 20000, 1 if before_wrapper else mode,
                        (before_layout, layout_read)[mode])
                    times[mode].append(elapsed)
                    sums.add(checksum)
                    if mode:
                        merged_counts.add(merged)
            assert len(sums) == 1
            assert merged_counts == ({20000} if name in ("469_514_view", "1156_scene") else {0})
            result = dict(layout=name, fields=fields, access=hex(access),
                          protect=hex(protect(ptr)), repeats=20000,
                          separate_ms=statistics.median(times[0]),
                          coalesced_ms=statistics.median(times[1]),
                          merged_counts=sorted(merged_counts), runs=times)
            if before_wrapper:
                result['baseline_ms'] = result.pop('separate_ms')
                result['candidate_ms'] = result.pop('coalesced_ms')
            layout_results.append(result)
            print(json.dumps(result), flush=True)
        for offset, size in ((0, 4), (0, 8), (0, 16), (8, 24), (0, 48), (0, 64), (4, 80), (0, 128), (0, 152)):
            times = {0: [], 1: []}
            sums = set()
            for repeat in range(3):
                for mode in ((0, 1) if repeat % 2 == 0 else (1, 0)):
                    checksum = C.c_uint64()
                    elapsed = (before_bench, bench)[mode](ptr, 4096, offset, size,
                        20000, 1 if before_wrapper else mode, C.byref(checksum))
                    assert elapsed >= 0
                    times[mode].append(elapsed)
                    sums.add(checksum.value)
            assert len(sums) == 1
            result = dict(storage=hex(flags), access=hex(access), protect=hex(protect(ptr)),
                          offset=offset, size=size, repeats=20000,
                          baseline_ms=statistics.median(times[0]),
                          candidate_ms=statistics.median(times[1]), runs=times)
            results.append(result)
            print(json.dumps(result), flush=True)
        donor = U()
        new(1, C.byref(donor))
        replacement = bytes((i * 11 + 23) % 256 for i in range(4096))
        storage(donor, 4096, replacement, 0)
        gpu_copy(donor, buffer, 0, 0, 4096)
        barrier(0x4000)
        finish()
        for offset, size in ((0, 4), (4, 8), (8, 16), (4092, 4), (4088, 8),
                             (4080, 16), (0, 24), (4, 80), (8, 152), (4072, 24)):
            assert expected(ptr, 4096, offset, size, replacement[offset:offset + size])
            gpu_cases += 1
        for fields in layouts.values():
            read_layout(ptr, 4096, fields, replacement, 1, 1)
            layout_gpu_cases += 1
        delete(1, C.byref(donor))
        assert unmap(buffer)
        delete(1, C.byref(buffer))
        assert error() == 0
heap_data = bytes(i % 256 for i in range(4096))
heap = C.create_string_buffer(heap_data)
for fields in layouts.values():
    _, _, merged = read_layout(C.addressof(heap), 4096, fields, heap_data, 1, 1)
    assert merged == 0
args.output.write_text(json.dumps(dict(results=results,
    layout_results=layout_results, layout_fresh_cases=layout_fresh_cases,
    layout_edge_cases=layout_edge_cases, layout_gpu_cases=layout_gpu_cases,
    non_wc_fallback_cases=len(layouts),
    renderer=renderer, equality_cases=equality_cases, fresh_value_cases=equality_cases,
    edge_cases=edge_cases, ordered_gpu_cases=gpu_cases,
    before=str(args.before) if args.before else None,
    comparison="earlier versus candidate with streaming/coalescing enabled" if args.before else "ordinary versus streaming / separate versus coalesced",
    limits="Independent AMD mappings and reflected grassland field layouts. No game FPS result."),
    indent=2), encoding="utf-8")
