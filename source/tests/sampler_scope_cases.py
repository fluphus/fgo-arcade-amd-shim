"""Driver checks for ordinary scopes using the real integrated sampler path."""
import ctypes as C
import statistics
import struct
from battle_window_driver_reflection import SystemGL


def check_scopes(wrapper, gl, program, state, read_image, material_buffer, material_bytes, material_offset, alternate_handle):
    U,I,P,F,Q=C.c_uint,C.c_int,C.c_void_p,C.c_float,C.c_uint64
    def api(name, result, *args): return SystemGL.api(wrapper,name,result,*args)
    fn=gl.proc
    seed=api('TestRegularSeedState',None,U)
    close=api('TestScopeClose',None)
    enable=api('TestScopeEnable',None,I)
    draw=api('TestScopeDraw',I,U,U)
    active=api('TestScopeActive',I)
    lookup=api('TestScopeProc',P,C.c_char_p)
    def hook(name,result,*args):
        address=lookup(name.encode()); assert address,name
        return C.WINFUNCTYPE(result,*args)(address)
    query=hook('glGetIntegerv',None,U,P)
    uniform=hook('glUniform4fv',None,I,I,P)
    use=hook('glUseProgram',None,U)
    bind_sampler=hook('glBindSampler',None,U,U)
    count=api('TestScopeCounters',None,P)
    close(); enable(1)
    fn('glUseProgram',None,U)(program)
    fn('glActiveTexture',None,U)(0x84C0+37)
    seed(program)
    saved=state()
    fn('glDrawArrays',None,U,I,I)(4,0,3);expected=read_image()
    enable(0);assert draw(program,1)==1
    control=read_image()
    if control!=expected:
        raise AssertionError('fixture original/private images differ before scope reuse is enabled')
    enable(1)
    before=(Q*3)();after=(Q*3)();count(before)
    assert draw(program,2000)==2000 and active()
    assert read_image()==expected
    value=I();fn('glGetIntegerv',None,U,P)(0x8B8D,C.byref(value))
    assert value.value!=program,'scope did not retain private program'
    query(0x8B8D,C.byref(value))
    assert value.value==program and not active() and state()==saved
    count(after)
    assert after[1]-before[1]>=1999

    # A material upload between retained draws must refresh the sampled handle.
    upload=hook('glNamedBufferSubData',None,U,C.c_ssize_t,C.c_ssize_t,P)
    changed_material=bytearray(material_bytes)
    struct.pack_into('<Q',changed_material,material_offset,alternate_handle)
    seed(program);assert draw(program,1)==1 and active()
    upload(material_buffer,0,len(changed_material),C.c_char_p(bytes(changed_material)))
    assert active() and draw(program,1)==1
    uploaded_image=read_image();close()
    fn('glDrawArrays',None,U,I,I)(4,0,3)
    assert uploaded_image==read_image()
    upload(material_buffer,0,len(material_bytes),C.c_char_p(bytes(material_bytes)))

    # Mirrored current-program vec4 writes address the application's original
    # even while the private program stays bound; next draw uploads dirty values.
    values=(F*4)();fn('glGetUniformfv',None,U,I,P)(program,0,values)
    changed=(F*4)(*values);changed[0]+=0.125
    seed(program);assert draw(program,1)==1 and active()
    uniform(0,1,changed)
    assert active()
    seen=(F*4)();fn('glGetUniformfv',None,U,I,P)(program,0,seen)
    assert bytes(seen)==bytes(changed)
    assert draw(program,1)==1;image=read_image();close()
    fn('glDrawArrays',None,U,I,I)(4,0,3);assert image==read_image()
    uniform(0,1,values)

    # Private-unit mutations and incompatible program selection restore first.
    seed(program);assert draw(program,1)==1 and active()
    old=I();fn('glGetIntegeri_v',None,U,U,P)(0x8919,64,C.byref(old))
    close();fn('glGetIntegeri_v',None,U,U,P)(0x8919,64,C.byref(old))
    assert draw(program,1)==1 and active()
    bind_sampler(64,old.value);assert not active()
    assert draw(program,1)==1 and active()
    use(program);assert not active()
    assert fn('glGetError',U)()==0

    measure=api('TestScopeMeasure',C.c_double,U,U,I)
    timings={0:[],1:[]}
    for run in range(10):
        for mode in ((0,1) if run%2==0 else (1,0)):
            cost=measure(program,2000,mode);assert cost>=0
            if run>=2:timings[mode].append(cost)
    enable(1);close()
    assert state()==saved
    return dict(reused_draws=int(after[1]-before[1]),
                state_and_pixels_equal=True,uniform_update_equal=True,material_upload_equal=True,
                completed_ms_2000={str(k):statistics.median(v) for k,v in timings.items()},
                trials=timings)
