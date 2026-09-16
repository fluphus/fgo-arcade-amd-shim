"""Replay captured trail bytes through the actual shim attribute preparation."""
import argparse
import ctypes as C
import json
from pathlib import Path
import numpy as np
from battle_window_driver_reflection import SystemGL

U,I,P,S=C.c_uint,C.c_int,C.c_void_p,C.c_ssize_t


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('wrapper',type=Path);ap.add_argument('sample',type=Path);ap.add_argument('output',type=Path)
    args=ap.parse_args();args.output.mkdir(parents=True,exist_ok=True)
    rows=json.loads((args.sample/'sample.json').read_text())['rows']
    resources=[r for r in rows if r['event']=='buffer' and r['role']=='vertex_prefix']
    source=next(r for r in resources if r['command']==0)
    stale=next(r for r in resources if r['command']==1)
    raw=(args.sample/f"blob-{source['blob']}.bin").read_bytes()
    stale_raw=(args.sample/f"blob-{stale['blob']}.bin").read_bytes()
    expected=np.frombuffer(raw,'<f4').reshape(-1,8);count=len(expected)
    with SystemGL() as gl:
        proc=gl.proc;dll=C.WinDLL(str(args.wrapper.resolve()))
        Resolve=C.WINFUNCTYPE(P,C.c_char_p);resolve=Resolve(gl.get_proc)
        gl.api(dll,'TestTrailConfigure',None,P,Resolve)(gl.gl._handle,resolve)
        replay=gl.api(dll,'TestTrailReplay',I,U,U,U,I,S,S,U,U,I)
        repeat=gl.api(dll,'TestTrailRepeat',None)
        shader=proc('glCreateShader',U,U)(0x8b31)
        code=b'#version 450\nlayout(location=0)in vec4 pos;layout(location=1)in vec4 uv;out vec4 p;out vec4 t;void main(){p=pos;t=uv;gl_Position=pos;}'
        text=C.c_char_p(code);size=I(len(code));proc('glShaderSource',None,U,I,C.POINTER(C.c_char_p),C.POINTER(I))(shader,1,C.byref(text),C.byref(size))
        proc('glCompileShader',None,U)(shader)
        program=proc('glCreateProgram',U)();proc('glAttachShader',None,U,U)(program,shader)
        names=(C.c_char_p*2)(b'p',b't');proc('glTransformFeedbackVaryings',None,U,I,C.POINTER(C.c_char_p),U)(program,2,names,0x8c8c)
        proc('glLinkProgram',None,U)(program);ok=I();proc('glGetProgramiv',None,U,U,C.POINTER(I))(program,0x8b82,C.byref(ok));assert ok.value
        proc('glUseProgram',None,U)(program)
        def buffer(data):
            b=U();proc('glCreateBuffers',None,I,C.POINTER(U))(1,C.byref(b));data=data.ljust(524288,b'\0')
            proc('glNamedBufferData',None,U,S,P,U)(b,len(data),data,0x88e4);return b.value
        vertex=buffer(raw);old=buffer(bytes(stale['offset'])+stale_raw);shifted=buffer(bytes(64)+raw)
        feedback=buffer(bytes(count*32));proc('glBindBufferBase',None,U,U,U)(0x8c8e,0,feedback)
        proc('glEnable',None,U)(0x8c89);results=[]
        cases=[('legacy',0,vertex,0,0,0,16,32),('fixed',1,vertex,0,0,0,16,32),
               ('cached',1,vertex,0,0,0,16,32),('relocated',1,shifted,512,64,0,16,32),
               ('split',1,vertex,0,0,1,16,32),('other_stride',1,vertex,0,0,0,16,36),
               ('other_offset',1,vertex,0,0,0,8,32)]
        for name,fixed,vb,core,nv,binding,relative,stride in cases:
            selected=replay(program,vb,old,fixed,core,nv,binding,relative,stride)
            wanted=name in ('fixed','cached','relocated');assert bool(selected)==wanted,(name,selected)
            if name=='cached':repeat()
            proc('glBeginTransformFeedback',None,U)(0);proc('glDrawArrays',None,U,I,I)(0,0,count);proc('glEndTransformFeedback',None)()
            actual=np.empty_like(expected);proc('glGetNamedBufferSubData',None,U,S,S,P)(feedback,0,actual.nbytes,actual.ctypes.data)
            assert proc('glGetError',U)()==0,name
            match=np.array_equal(actual,expected)
            if wanted:assert match,name
            if name=='legacy':assert not match and (~np.isfinite(actual[:,4:6])).any()
            results.append(dict(case=name,selected=bool(selected),matches_requested_bytes=match,nonfinite_uv=int((~np.isfinite(actual[:,4:6])).sum())))
        result=dict(passed=True,driver=proc('glGetString',C.c_char_p,U)(0x1f01).decode(),vertices=count,cases=results)
    (args.output/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))


if __name__=='__main__':main()
