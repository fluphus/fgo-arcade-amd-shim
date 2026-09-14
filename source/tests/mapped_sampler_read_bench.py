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
args = ap.parse_args()
wrapper = C.WinDLL(str(args.wrapper.resolve()))
U, S, P = C.c_uint, C.c_size_t, C.c_void_p
bench = SystemGL.api(wrapper, "TestMappedSamplerRead", C.c_double,
                     P, S, S, S, U, C.c_int, C.POINTER(C.c_uint64))
equal = SystemGL.api(wrapper, "TestMappedSamplerEqual", C.c_int, P, S, S, S)
expected = SystemGL.api(wrapper, "TestMappedSamplerExpected", C.c_int, P, S, S, S, P)
protect = SystemGL.api(wrapper, "TestMappedMemoryProtect", U, P)
results = []
edge_cases = 0
gpu_cases = 0
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
                for size in (4, 8, 16, 24, 48, 80):
                    assert expected(ptr, 4096, offset, size, data[offset:offset + size])
                    assert equal(ptr, 4096, offset, size), (access, generation, offset, size)
            for shift, backing in ((1, 4094), (8, 4087), (0, 4093)):
                for offset in (0, 3, backing - 24):
                    assert expected(ptr + shift, backing, offset, 24,
                                    data[shift + offset:shift + offset + 24])
                    edge_cases += 1
        for offset, size in ((0, 4), (0, 8), (0, 16), (8, 24), (0, 48), (4, 80), (0, 152)):
            times = {0: [], 1: []}
            sums = set()
            for repeat in range(3):
                for mode in ((0, 1) if repeat % 2 == 0 else (1, 0)):
                    checksum = C.c_uint64()
                    elapsed = bench(ptr, 4096, offset, size, 20000, mode, C.byref(checksum))
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
        for offset, size in ((0, 24), (4, 80), (8, 152), (4072, 24)):
            assert expected(ptr, 4096, offset, size, replacement[offset:offset + size])
            gpu_cases += 1
        delete(1, C.byref(donor))
        assert unmap(buffer)
        delete(1, C.byref(buffer))
        assert error() == 0
args.output.write_text(json.dumps(dict(results=results,
    renderer=renderer, equality_cases=252, fresh_value_cases=252,
    edge_cases=edge_cases, ordered_gpu_cases=gpu_cases,
    limits="Independent AMD mappings; current capture did not record map flags. No game FPS result."),
    indent=2), encoding="utf-8")
