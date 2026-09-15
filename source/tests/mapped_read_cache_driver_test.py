"""Validate current bytes across cache boundaries and time actual AMD mappings."""
import ctypes as C
import json
from pathlib import Path
import statistics
import sys
from battle_window_driver_reflection import SystemGL

out=Path(sys.argv[1]).resolve(); lib=C.WinDLL(str(out/'wrapper.dll'))
U,I,S,P= C.c_uint,C.c_int,C.c_ssize_t,C.c_void_p
api=lambda name,result,*args:SystemGL.api(lib,name,result,*args)
setup=api('TestMappedCacheSetup',None,P,P)
mapped=api('TestMappedCacheMap',P,U,S,U)
read=api('TestMappedCacheRead',I,U,S,S,P)
boundary=api('TestMappedCacheBoundary',None,U,U,P)
write=api('TestMappedCacheWrite',None,U,U,U,P)
release=api('TestMappedCacheRelease',None,U,I)
remap=api('TestMappedCacheRemap',P,U)
counter=api('TestMappedCacheCounter',C.c_uint64,U)
bench=api('TestMappedCacheBench',C.c_double,U,U,I,U,C.POINTER(C.c_uint64))
passed=[]; rows=[]
with SystemGL() as gl:
    new=gl.proc('glCreateBuffers',None,I,P)
    finish=gl.proc('glFinish',None)
    error=gl.proc('glGetError',U)
    fence=gl.proc('glFenceSync',P,U,U)
    delete_sync=gl.proc('glDeleteSync',None,P)
    upload=gl.proc('glNamedBufferSubData',None,U,S,S,P)
    setup(gl.gl._handle,gl.get_proc)
    buf=U();new(1,C.byref(buf));ptr=mapped(buf,4096,0x52);assert ptr
    C.memmove(ptr,bytes([17])*4096,4096);boundary(1,buf,None)
    # Match the real object's SSBO exposure without treating a bind as a write.
    api('TestRegularGPUBuffer',None,U)(buf)
    passed.append('GPU-exposed buffer')

    def current(offset,size,expected,hit=None):
        before=counter(0);value=(C.c_ubyte*size)()
        assert read(buf,offset,size,value)==1
        assert bytes(value)==expected,(offset,size,bytes(value),expected)
        if hit is not None:assert counter(0)-before==int(hit),(offset,size,hit)

    for size in [4,8,16,40,48,64,152,256]:
        boundary(0,buf,None)
        current(64,size,bytes([17])*size,False)
        current(64,size,bytes([17])*size,True)
        passed.append(f'exact {size} bytes')
    current(4000,8,bytes([17])*8,False)
    current(4000,8,bytes([17])*8,True)
    before=counter(0);value=(C.c_ubyte*8)()
    assert read(buf,4093,8,value)==0 and read(buf,-1,8,value)==0
    assert counter(0)==before
    passed.append('map edges')

    for kind in range(10):
        old=bytes([30+kind])*48;new_data=bytes([31+kind])*48
        C.memmove(ptr+64,old,48);boundary(1,buf,None)
        current(64,48,old,False);current(64,48,old,True)
        C.memmove(ptr+64,new_data,48)
        sync=fence(0x9117,0) if kind in (7,8) else None
        boundary(kind,buf,sync)
        if sync:finish();delete_sync(sync)
        current(64,48,new_data,False);current(64,48,new_data,True)
        assert error()==0,kind
        passed.append(f'publication boundary {kind}')

    source=U();new(1,C.byref(source))
    gl.proc('glNamedBufferStorage',None,U,S,P,U)(source,48,None,0x100)
    for kind in range(7):
        old=bytes([50+kind])*48;new_data=bytes([51+kind])*48
        C.memmove(ptr+64,old,48);boundary(1,buf,None)
        current(64,48,old,False);current(64,48,old,True)
        data=C.create_string_buffer(new_data)
        upload(source,0,48,data)
        write(kind,buf,source,data);finish()
        current(64,48,new_data,False);current(64,48,new_data,True)
        assert error()==0,('write',kind)
        passed.append(f'GPU/CPU write {kind}')

    release(buf,0);assert read(buf,64,8,value)==0
    ptr=remap(buf);assert ptr
    C.memmove(ptr,bytes([17])*4096,4096);boundary(1,buf,None)
    current(64,48,bytes([17])*48,False);current(64,48,bytes([17])*48,True)
    passed.append('unmap/remap')

    for period in (() if '--checks-only' in sys.argv else (24,1,0)):
        for trial in range(7):
            for enabled in ([0,1] if trial%2==0 else [1,0]):
                hits=counter(0);misses=counter(1);checksum=C.c_uint64()
                ms=bench(buf,20000,enabled,period,C.byref(checksum))
                assert ms>0 and checksum.value==680000
                rows.append(dict(period=period,trial=trial,enabled=enabled,ms=ms,
                                 hits=counter(0)-hits,misses=counter(1)-misses))
    release(buf,1);assert read(buf,64,8,value)==0
    gl.proc('glDeleteBuffers',None,I,P)(1,C.byref(source))
    passed.append('delete')
    buf=U();new(1,C.byref(buf));ptr=mapped(buf,4096,0xc2);assert ptr
    for val in (17,19,23):
        C.memmove(ptr+64,bytes([val])*48,48)
        current(64,48,bytes([val])*48,False)
    release(buf,1);passed.append('coherent map stays fresh')
    assert error()==0
medians={}
for period in sorted({r['period'] for r in rows}):
    m={enabled:statistics.median(r['ms'] for r in rows if r['period']==period and r['enabled']==enabled and r['trial']>0) for enabled in (0,1)}
    medians[period]=dict(before_ms=m[0],after_ms=m[1],speedup=m[0]/m[1])
result=dict(passed=passed,rows=rows,medians=medians)
(out/('driver-exposed-checks.json' if '--checks-only' in sys.argv else 'driver-test.json')).write_text(json.dumps(result,indent=2),encoding='utf-8')
print(json.dumps(dict(cases=len(passed),medians=medians),indent=2))
