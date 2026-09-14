"""Measure repeated mapped reads versus one flush-range copy on the AMD driver."""
import argparse
import ctypes as C
import json
from pathlib import Path
import statistics
from battle_window_driver_reflection import SystemGL


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--dll',type=Path,required=True)
    ap.add_argument('--output',type=Path,required=True)
    ap.add_argument('--stream-only',action='store_true')
    args=ap.parse_args()
    dll=C.WinDLL(str(args.dll.resolve()))
    bench=SystemGL.api(dll,'TestMemoryReadBenchmark',C.c_double,
                       C.c_void_p,C.c_size_t,C.c_uint,C.c_uint,C.c_int,
                       C.POINTER(C.c_uint64))
    length=1024*1024
    initial=(C.c_ubyte*length)(*([37]*length))
    report={}
    with SystemGL() as gl:
        get_error=gl.proc('glGetError',C.c_uint)
        name=C.c_uint()
        gl.proc('glGenBuffers',None,C.c_int,C.c_void_p)(1,C.byref(name))
        gl.proc('glBindBuffer',None,C.c_uint,C.c_uint)(0x8A11,name)
        gl.proc('glBufferStorage',None,C.c_uint,C.c_ssize_t,C.c_void_p,C.c_uint)(0x8A11,length,initial,0x42)
        mapped=gl.proc('glMapBufferRange',C.c_void_p,C.c_uint,C.c_ssize_t,C.c_ssize_t,C.c_uint)(0x8A11,0,length,0x52)
        assert mapped and not get_error()
        C.memmove(mapped,initial,length)
        gl.proc('glFlushMappedBufferRange',None,C.c_uint,C.c_ssize_t,C.c_ssize_t)(0x8A11,0,length)
        assert not get_error()
        for repeats in ((4,) if args.stream_only else (1,4,16)):
            variants={}
            expected=None
            for mode in ((0,3,4) if args.stream_only else (0,1,2)):
                times=[]
                for _ in range(3):
                    checksum=C.c_uint64()
                    times.append(bench(mapped,length,10,repeats,mode,C.byref(checksum)))
                    if expected is None:expected=checksum.value
                    assert checksum.value==expected
                variants[str(mode)]={'median_ms':statistics.median(times),'runs_ms':times}
            report[str(repeats)]=variants
        gl.proc('glUnmapBuffer',C.c_ubyte,C.c_uint)(0x8A11)
        gl.proc('glDeleteBuffers',None,C.c_int,C.c_void_p)(1,C.byref(name))
        assert not get_error()
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(report,indent=2))
    print(json.dumps(report,indent=2))


if __name__=='__main__':main()
