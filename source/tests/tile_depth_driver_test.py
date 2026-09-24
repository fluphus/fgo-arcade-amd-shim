"""Compare the production tile-depth path with the original shader on AMD OpenGL."""
import argparse
import ctypes as C
import hashlib
import json
import os
from pathlib import Path
import statistics
import sys
import time
import numpy as np

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tests'))
from battle_window_driver_reflection import SystemGL
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--wrapper',type=Path,required=True)
parser.add_argument('--output',type=Path,required=True)
parser.add_argument('--no-near',action='store_true')
args=parser.parse_args()
OUT=args.output.resolve()
OUT.mkdir(parents=True,exist_ok=True)
config=ROOT.parent/'game-patch/amdcfg'
if config.is_dir(): os.environ['AMD_CONFIG_DIR']=str(config)
SOURCE=ROOT/'tests/fixtures/tile/depth.vert'
WRAPPER=args.wrapper.resolve()
NO_NEAR=args.no_near
original=SOURCE.read_bytes()
if NO_NEAR:
    original=original.replace(b'#define USE_PER_TILE_MIN_DEPTH 1',b'#define USE_PER_TILE_MIN_DEPTH 0')
start=original.index(b'\tfloat df = 10000.f;')
end=original.index(b'#endif//USE_MINMAX_FILTER',start)+len(b'#endif//USE_MINMAX_FILTER')
parallel=(original[:start]+b'\tvec2 bounds=tile_depth[tile_y_idx*32u+tile_x_idx];\n\tfloat df=bounds.x;\n\tfloat dn=bounds.y;'+original[end:])
parallel=parallel.replace(b'void main(){',b'layout(std430,binding=47) readonly buffer TileDepth {vec2 tile_depth[];};\nvoid main(){',1)
readonly=original.replace(b'binding = 36) buffer LightData',b'binding = 36) readonly buffer LightData')
assert readonly!=original
compute=b'''#version 430
#define USE_NEAR 1
layout(local_size_x=32) in;
layout(binding=0) uniform sampler2D opaque_depth;
layout(binding=1) uniform sampler2D full_depth;
layout(location=0) uniform float resolution_scale;
layout(std430,binding=47) writeonly buffer TileDepth {vec2 tile_depth[];};
shared float row_near[32],row_far[32];
void main(){
 uint row=gl_LocalInvocationID.x;
 vec2 offset=vec2(1.f/1920.f,1.f/1080.f);
 vec2 uv=vec2(float(gl_WorkGroupID.x*60u)*offset.x,float(gl_WorkGroupID.y*60u)*offset.y);
 uv*=resolution_scale;
 offset*=resolution_scale;
 offset*=2.f;
 float max_uv=resolution_scale-1.f/1080.f;
 vec2 t=uv;
 for(uint i=0u;i<row;i++)t.y=min(t.y+offset.y,max_uv);
 float dn=0.f,df=10000.f;
 if(row<31u){
#if USE_NEAR
  for(int j=0;j<=30;j++){dn=max(dn,textureLod(full_depth,t,0.f).r);t.x=min(t.x+offset.x,max_uv);}
#endif
  t.x=uv.x;
  for(int j=0;j<=30;j++){df=min(df,textureLod(opaque_depth,t,0.f).r);t.x=min(t.x+offset.x,max_uv);}
 }
 row_near[row]=dn;row_far[row]=df;
 barrier();
 for(uint step=16u;step>0u;step>>=1u){
  if(row<step){row_near[row]=max(row_near[row],row_near[row+step]);row_far[row]=min(row_far[row],row_far[row+step]);}
  barrier();
 }
 if(row==0u)tile_depth[gl_WorkGroupID.y*32u+gl_WorkGroupID.x]=vec2(row_far[0],row_near[0]);
}
'''
if NO_NEAR: compute=compute.replace(b'#define USE_NEAR 1',b'#define USE_NEAR 0')
for name,source in [('tile-original.vert',original),('tile-parallel.vert',parallel),('tile-reduce.comp',compute)]:
    (OUT/(('no-near-' if NO_NEAR else '')+name)).write_bytes(source)
U,I,P,F=C.c_uint,C.c_int,C.c_void_p,C.c_float

with SystemGL() as gl:
    fn=gl.proc
    report=dict(renderer=fn('glGetString',C.c_char_p,U)(0x1F01).decode(),source=str(SOURCE),
                source_sha256=hashlib.sha256(original).hexdigest(),cases=[],timings={})
    def program(source,kind):
        sh=fn('glCreateShader',U,U)(kind);ptr=C.c_char_p(source)
        fn('glShaderSource',None,U,I,P,P)(sh,1,C.byref(ptr),None)
        fn('glCompileShader',None,U)(sh)
        ok=I();fn('glGetShaderiv',None,U,U,P)(sh,0x8B81,C.byref(ok))
        if not ok.value:
            log=C.create_string_buffer(8192);fn('glGetShaderInfoLog',None,U,I,P,P)(sh,len(log),None,log)
            raise RuntimeError(log.value.decode(errors='replace'))
        pr=fn('glCreateProgram',U)();fn('glAttachShader',None,U,U)(pr,sh)
        fn('glLinkProgram',None,U)(pr)
        fn('glGetProgramiv',None,U,U,P)(pr,0x8B82,C.byref(ok))
        if not ok.value:
            log=C.create_string_buffer(8192);fn('glGetProgramInfoLog',None,U,I,P,P)(pr,len(log),None,log)
            raise RuntimeError(log.value.decode(errors='replace'))
        return pr
    progs={name:program(src,0x8B31) for name,src in [('original',original),('readonly',readonly),('parallel',parallel)]}
    comp=program(compute,0x91B9)
    if WRAPPER:
        wrapper=C.WinDLL(str(WRAPPER))
        install=SystemGL.api(wrapper,'TestTileInstall',I,P,P,U,C.c_char_p)
        tile_uniform=SystemGL.api(wrapper,'TestTileUniform',None,U,P)
        tile_draw=SystemGL.api(wrapper,'TestTileDraw',I,U,I)
        assert install(gl.gl._handle,gl.get_proc,progs['original'],original)==1

    def buffer(size,binding):
        b=U();fn('glCreateBuffers',None,I,P)(1,C.byref(b))
        fn('glNamedBufferData',None,U,C.c_ssize_t,P,U)(b,size,None,0x88E8)
        fn('glBindBufferBase',None,U,U,U)(0x90D2,binding,b)
        return b.value
    tiles=buffer(1152*8,0);indices=buffer(1152*48*4,1);lights=buffer(64*48,36);depths=buffer(576*8,47)
    if WRAPPER:
        fn('glBindBufferRange',None,U,U,U,C.c_ssize_t,C.c_ssize_t)(0x90D2,47,depths,256,1024)
        fn('glBindBuffer',None,U,U)(0x90D2,lights)
    vao=U();fn('glGenVertexArrays',None,I,P)(1,C.byref(vao));fn('glBindVertexArray',None,U)(vao)
    fn('glEnable',None,U)(0x8C89)
    # Distinct finite depth patterns, with the exact source texture/sampler
    # coordinates and min/max operations preserved.
    y,x=np.mgrid[0:1080,0:1920]
    textures=[]
    for unit in range(2):
        t=U();fn('glCreateTextures',None,U,I,P)(0xDE1,1,C.byref(t))
        fn('glTextureStorage2D',None,U,I,U,I,I)(t,1,0x822E,1920,1080)
        for enum in (0x2801,0x2800): fn('glTextureParameteri',None,U,U,I)(t,enum,0x2600)
        for enum in (0x2802,0x2803): fn('glTextureParameteri',None,U,U,I)(t,enum,0x812F)
        data=(.05+.85*((x*7+y*(11+unit*2))%997)/996).astype(np.float32)
        fn('glTextureSubImage2D',None,U,I,I,I,I,I,U,U,P)(t,0,0,0,1920,1080,0x1903,0x1406,data.ctypes.data)
        fn('glBindTextureUnit',None,U,U)(unit,t)
        textures.append(t.value)
    constants=np.zeros((11,4),dtype=np.float32)
    constants[0,:3]=[0,0,1];constants[1,3]=.5
    constants[2:10,:3]=[[-1,-1,1],[-1,1,1],[1,-1,1],[1,1,1],[-100,-100,100],[-100,100,100],[100,-100,100],[100,100,100]]
    constants[2,3]=1;constants[3,3]=99;constants[4,3]=1
    constants[10,2:]=[.99,.01]
    light_data=np.zeros((64,12),dtype=np.float32)
    for i in range(64):
        light_data[i,:4]=[((i%8)-3.5)*2,((i//8)-3.5)*2,2+i%12,5+i%7]
        light_data[i,4:8]=[1,.7,.4,1]
        if i%3==0:
            light_data[i].view(np.uint32)[8]=65536
            light_data[i].view(np.uint32)[9]=0
            light_data[i].view(np.uint32)[10]=int(np.array([1,0],np.float16).view(np.uint32)[0])
    upload=fn('glNamedBufferSubData',None,U,C.c_ssize_t,C.c_ssize_t,P)
    upload(lights,0,light_data.nbytes,light_data.ctypes.data)
    get=fn('glGetNamedBufferSubData',None,U,C.c_ssize_t,C.c_ssize_t,P)
    use=fn('glUseProgram',None,U);draw=fn('glDrawArrays',None,U,I,I)
    dispatch=fn('glDispatchCompute',None,U,U,U);barrier=fn('glMemoryBarrier',None,U)
    finish=fn('glFinish',None);error=fn('glGetError',U)
    def run(mode,n=1):
        for _ in range(n):
            if WRAPPER and mode=='parallel':
                assert tile_draw(progs['original'],1)==1
                v=I();q=C.c_int64()
                fn('glGetIntegerv',None,U,P)(0x8B8D,C.byref(v));assert v.value==progs['original']
                fn('glGetIntegeri_v',None,U,U,P)(0x90D3,47,C.byref(v));assert v.value==depths
                fn('glGetInteger64i_v',None,U,U,P)(0x90D4,47,C.byref(q));assert q.value==256
                fn('glGetInteger64i_v',None,U,U,P)(0x90D5,47,C.byref(q));assert q.value==1024
                fn('glGetIntegerv',None,U,P)(0x90D3,C.byref(v));assert v.value==lights
                barrier(0x2000)
                continue
            if mode=='parallel':
                use(comp);dispatch(32,18,1);barrier(0x2000)
            use(progs[mode]);draw(0,0,1152);barrier(0x2000)
    sentinel=bytes([0xCD])*(1152*48*4)
    extensions=fn('glGetString',C.c_char_p,U)(0x1F03).split()
    has_minmax=b'GL_ARB_texture_filter_minmax' in extensions or b'GL_EXT_texture_filter_minmax' in extensions
    report['filter_minmax_supported']=has_minmax
    cases=[(name,scale,count) for name in (['nearest','linear','minmax'] if has_minmax else ['nearest','linear'])
           for scale,count in [(1,0),(1,1),(1,16),(1,48),(.5,16),(1.5,32)]]
    for filtering,scale,count in cases:
        for unit,t in enumerate(textures):
            for enum in (0x2801,0x2800):
                fn('glTextureParameteri',None,U,U,I)(t,enum,0x2600 if filtering=='nearest' else 0x2601)
            if has_minmax:
                fn('glTextureParameteri',None,U,U,I)(t,0x9366,(0x8007+unit) if filtering=='minmax' else 0x9367)
        constants[0,3]=count;constants[5,3]=scale
        for p in progs.values(): fn('glProgramUniform4fv',None,U,I,I,P)(p,0,11,constants.ctypes.data)
        fn('glProgramUniform1f',None,U,I,F)(comp,0,scale)
        if WRAPPER: tile_uniform(progs['original'],constants.ctypes.data)
        results={}
        for mode in progs:
            upload(tiles,0,1152*8,C.c_char_p(sentinel))
            upload(indices,0,len(sentinel),C.c_char_p(sentinel))
            run(mode);finish()
            a=C.create_string_buffer(1152*8);b=C.create_string_buffer(len(sentinel))
            get(tiles,0,len(a),a);get(indices,0,len(b),b)
            assert error()==0,(mode,scale,count)
            results[mode]=a.raw+b.raw
        assert results['original']==results['readonly'],('readonly mismatch',scale,count)
        assert results['original']==results['parallel'],('parallel mismatch',scale,count)
        counts=np.frombuffer(results['original'][:1152*8],np.int32).reshape(-1,2)[:,0]
        report['cases'].append(dict(filtering=filtering,scale=scale,lights=count,max_tile_lights=int(counts.max()),
                                   total_references=int(counts.sum()),sha256=hashlib.sha256(results['original']).hexdigest()))
    constants[0,3]=48;constants[5,3]=1
    for p in progs.values(): fn('glProgramUniform4fv',None,U,I,I,P)(p,0,11,constants.ctypes.data)
    fn('glProgramUniform1f',None,U,I,F)(comp,0,1)
    if WRAPPER: tile_uniform(progs['original'],constants.ctypes.data)
    report['timing_case']=dict(filtering='minmax' if has_minmax else 'linear',scale=1,lights=48)
    measured={k:[] for k in progs}
    for trial in range(16):
        for mode in (list(progs) if trial%2==0 else list(reversed(progs))):
            finish();begin=time.perf_counter_ns();run(mode,8);finish()
            cost=(time.perf_counter_ns()-begin)/8e6
            if trial>=4: measured[mode].append(cost)
    assert error()==0
    report['integrated_wrapper']=str(WRAPPER) if WRAPPER else None
    report['timings']={k:dict(median_ms=statistics.median(v),runs_ms=v) for k,v in measured.items()}
    report['limits']=['Captured shader variants are matched by source fingerprints in the production path.',
                      'Finite R32F depth textures with nearest/linear and supported minmax filtering; not complete game rendering validation.',
                      'Pipeline timings include dispatch/barrier/VS completion; no timestamp queries inserted.',
                      'No deployed code, shader, machine setting or game process changed.']
    (OUT/(('integrated-' if WRAPPER else '')+('tile-depth-no-near-result.json' if NO_NEAR else 'tile-depth-filter-result.json'))).write_text(json.dumps(report,indent=2),encoding='utf-8')
    print(json.dumps(dict(renderer=report['renderer'],cases_passed=len(report['cases']),timing_case=report['timing_case'],
                          medians_ms={k:v['median_ms'] for k,v in report['timings'].items()}),indent=2))
