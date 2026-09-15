"""Run the captured London fog compute shader through old/fixed dispatch wrappers."""
import ctypes as C
import json
from pathlib import Path
import struct
import sys
import numpy as np
from battle_window_driver_reflection import SystemGL

U,I,P,S = C.c_uint,C.c_int,C.c_void_p,C.c_ssize_t


def main():
    dllpath, shaderpath, outpath = map(Path,sys.argv[1:4])
    dll=C.WinDLL(str(dllpath.resolve()))
    setup=SystemGL.api(dll,'TestComputeSetup',None,P,P)
    track=SystemGL.api(dll,'TestComputeTrack',None,U,U,C.c_char_p,I)
    cache=SystemGL.api(dll,'TestComputeCache',None,I)
    address=SystemGL.api(dll,'TestComputeAddress',C.c_uint64,U,U)
    upload=SystemGL.api(dll,'TestComputeData',None,U,S,P)
    sub=SystemGL.api(dll,'TestComputeSubData',None,U,S,S,P)
    bind=SystemGL.api(dll,'TestComputeBind',None,U,U,U,S,S)
    use=SystemGL.api(dll,'TestComputeUse',None,U)
    dispatch=SystemGL.api(dll,'TestComputeDispatch',None,U,U,U)
    results=[]
    with SystemGL() as gl:
        setup(gl.gl._handle,C.cast(gl.get_proc,P))
        def program(source,pointers):
            sh=gl.proc('glCreateShader',U,U)(0x91b9)
            text=C.c_char_p(source);n=I(len(source))
            gl.proc('glShaderSource',None,U,I,C.POINTER(C.c_char_p),C.POINTER(I))(sh,1,C.byref(text),C.byref(n))
            gl.proc('glCompileShader',None,U)(sh)
            status=I();gl.proc('glGetShaderiv',None,U,U,C.POINTER(I))(sh,0x8b81,C.byref(status))
            if not status.value:
                log=C.create_string_buffer(16384);gl.proc('glGetShaderInfoLog',None,U,I,P,P)(sh,len(log),None,log);raise RuntimeError(log.value)
            prog=gl.proc('glCreateProgram',U)()
            gl.proc('glAttachShader',None,U,U)(prog,sh);gl.proc('glLinkProgram',None,U)(prog)
            gl.proc('glGetProgramiv',None,U,U,C.POINTER(I))(prog,0x8b82,C.byref(status))
            if not status.value:
                log=C.create_string_buffer(16384);gl.proc('glGetProgramInfoLog',None,U,I,P,P)(prog,len(log),None,log);raise RuntimeError(log.value)
            track(prog,sh,source,pointers)
            return prog
        fog=program(shaderpath.read_bytes(),1)
        plain=program(b'#version 450\nlayout(local_size_x=1) in; layout(binding=0,rgba16f) uniform image3D dst; void main(){imageStore(dst,ivec3(0),vec4(3,2,1,1));}',0)
        def buffer(data):
            b=U();gl.proc('glCreateBuffers',None,I,C.POINTER(U))(1,C.byref(b));upload(b,len(data),data);return b.value
        light=lambda rgb:struct.pack('<12f',0,0,2,10,*rgb,0,15,0,0,0)
        good=buffer(light([16,8,4]));other=buffer(bytes(256)+light([4,12,20]));bad=buffer(light([6400,6400,6400]));proxy=buffer(bytes(1024))
        def field(name):
            text=C.c_char_p(name.encode());idx=U();off=I()
            gl.proc('glGetUniformIndices',None,U,I,C.POINTER(C.c_char_p),C.POINTER(U))(fog,1,C.byref(text),C.byref(idx))
            assert idx.value!=0xffffffff,name
            gl.proc('glGetActiveUniformsiv',None,U,I,C.POINTER(U),U,C.POINTER(I))(fog,1,C.byref(idx),0x8a3b,C.byref(off))
            return off.value
        scene=bytearray(1024);camera=bytearray(1024);shadow=bytearray(1024)
        struct.pack_into('<4f',scene,field('g_light_modifier'),0,100,1,1)
        projection=field('g_projection_view[0]')
        struct.pack_into('<16f',camera,projection,1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1)
        struct.pack_into('<4f',shadow,field('g_parallel_light_dir'),0,1,0,0)
        struct.pack_into('<2Q',scene,840,address(good,0),address(proxy,0))
        ubos=[buffer(bytes(scene)),buffer(bytes(camera)),buffer(bytes(shadow))]
        scene_b=bytearray(scene);struct.pack_into('<Q',scene_b,840,address(other,256));ubo_b=buffer(bytes(1024)+bytes(scene_b))
        for i,b in enumerate(ubos):bind(0x8a11,i,b,0,1024)
        def texture(target,width,height,depth,internal,data=None):
            t=U();gl.proc('glCreateTextures',None,U,I,C.POINTER(U))(target,1,C.byref(t))
            if target==0x806f:
                gl.proc('glTextureStorage3D',None,U,I,U,I,I,I)(t,1,internal,width,height,depth)
                if data:gl.proc('glTextureSubImage3D',None,U,I,I,I,I,I,I,I,U,U,P)(t,0,0,0,0,width,height,depth,0x1908,0x1406,data)
            else:
                gl.proc('glTextureStorage2D',None,U,I,U,I,I)(t,1,internal,width,height)
                if data:gl.proc('glTextureSubImage2D',None,U,I,I,I,I,I,U,U,P)(t,0,0,0,width,height,0x1908,0x1406,data)
            for name in [0x2800,0x2801]:gl.proc('glTextureParameteri',None,U,U,I)(t,name,0x2600)
            return t.value
        output=texture(0x806f,5,30,1,0x881a)
        noise=texture(0x806f,1,1,1,0x8814,struct.pack('<4f',1,1,1,1))
        shtex=texture(0x0de1,1,1,1,0x8814,struct.pack('<4f',1,1,1,1))
        gl.proc('glBindTextureUnit',None,U,U)(0,noise)
        for unit in [1,2,3]:gl.proc('glBindTextureUnit',None,U,U)(unit,shtex)
        for unit,fmt,b in [(20,0x823b,buffer(struct.pack('<2i',1,0)*1152)),(21,0x8235,buffer(struct.pack('<i',0)))]:
            t=U();gl.proc('glCreateTextures',None,U,I,C.POINTER(U))(0x8c2a,1,C.byref(t))
            gl.proc('glTextureBuffer',None,U,U,U)(t,fmt,b);gl.proc('glBindTextureUnit',None,U,U)(unit,t)
        gl.proc('glBindImageTexture',None,U,U,I,C.c_ubyte,I,U,U)(0,output,0,1,0,0x88b9,0x881a)
        constants=np.zeros((14,4),dtype='<f4')
        constants[0]=[0,0,1,1];constants[3,3]=1
        constants[6,0]=1;constants[8]=[0,0,1,1]
        constants[9,3]=1;constants[10,:3]=1;constants[11,0]=1;constants[13,1]=1
        gl.proc('glProgramUniform4fv',None,U,I,I,P)(fog,0,14,constants.ctypes.data)
        def run(prog):
            use(prog);dispatch(1,1,1)
            gl.proc('glMemoryBarrier',None,U)(0x128)
            pixels=np.empty((150,4),dtype='<f4')
            gl.proc('glGetTextureImage',None,U,I,U,U,I,P)(output,0,0x1908,0x1406,pixels.nbytes,pixels.ctypes.data)
            error=gl.proc('glGetError',U)();assert error==0,hex(error)
            assert np.isfinite(pixels).all()
            return pixels
        def direct_reference(b,offset):
            gl.proc('glBindBufferRange',None,U,U,U,S,S)(0x90d2,30,b,offset,48)
            gl.proc('glUseProgram',None,U)(fog);gl.proc('glDispatchCompute',None,U,U,U)(1,1,1)
            gl.proc('glMemoryBarrier',None,U)(0x128)
            pixels=np.empty((150,4),dtype='<f4')
            gl.proc('glGetTextureImage',None,U,I,U,U,I,P)(output,0,0x1908,0x1406,pixels.nbytes,pixels.ctypes.data)
            return pixels
        ref_good=direct_reference(good,0);ref_other=direct_reference(other,256)
        assert 0<ref_good[0,0]<10 and not np.array_equal(ref_good,ref_other)
        for enabled in [0,1]:
            cache(enabled)
            sub(ubos[0],840,8,struct.pack('<Q',address(good,0)))
            bind(0x8a11,0,ubos[0],0,1024);bind(0x90d2,30,bad,0,48)
            a=run(fog)
            bind(0x8a11,0,ubo_b,1024,1024)
            b=run(fog)
            sub(ubo_b,1024+840,8,struct.pack('<Q',address(good,0)))
            c=run(fog)
            sub(ubo_b,1024+840,8,struct.pack('<Q',address(other,256)))
            d=run(plain)
            results.append(dict(cache=enabled,stale_binding_fixed=bool(np.array_equal(a,ref_good)),
                                ubo_range_changed=bool(np.array_equal(b,ref_other)),
                                ubo_contents_changed=bool(np.array_equal(c,ref_good)),
                                pointer_free=bool(np.array_equal(d[0],[3,2,1,1])),
                                stale_pixel=a[0].tolist(),expected_pixel=ref_good[0].tolist()))
        report=dict(shader=str(shaderpath),results=results,
                    all_fixed=all(all(r[k] for k in ['stale_binding_fixed','ubo_range_changed','ubo_contents_changed','pointer_free']) for r in results))
        outpath.write_text(json.dumps(report,indent=2)+'\n')
        print(json.dumps(report))
        assert report['all_fixed'], 'Compute pointer regression failed; see result JSON'


if __name__=='__main__':main()
