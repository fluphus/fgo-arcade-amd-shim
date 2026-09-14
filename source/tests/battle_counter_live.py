import argparse, ctypes as C, ctypes.wintypes as W, datetime as dt, json, struct, subprocess, time
from pathlib import Path
import pefile

def main(a):
    mods=json.loads(a.modules.read_text(encoding='utf-8-sig'))
    r=next(x for x in mods if x['FileName'].lower()==str(a.renderer).lower())
    base=int(r['Base']); binary=a.symbol_file or Path(r['FileName'])
    pe_base=pefile.PE(str(binary),fast_load=True).OPTIONAL_HEADER.ImageBase; syms={}
    for line in subprocess.check_output(['llvm-nm.exe','--defined-only',str(binary)],text=True).splitlines():
        f=line.split()
        if len(f)==3: syms[f[2]]=int(f[0],16)-pe_base
    k=C.WinDLL('kernel32',use_last_error=True); k.OpenProcess.argtypes=[W.DWORD,W.BOOL,W.DWORD]; k.OpenProcess.restype=W.HANDLE
    k.ReadProcessMemory.argtypes=[W.HANDLE,W.LPCVOID,W.LPVOID,C.c_size_t,C.POINTER(C.c_size_t)]; k.ReadProcessMemory.restype=W.BOOL
    h=k.OpenProcess(0x410,False,a.pid)
    if not h: raise C.WinError(C.get_last_error())
    names=['g_frame_count','g_perf_pointer_exact_cache_hits','g_perf_pointer_exact_cache_misses','g_perf_pointer_shadow_reads','g_perf_cpu_shadow_reads','g_perf_mapped_flush_shadow_reads','g_perf_mapped_flush_shadow_flushes','g_perf_mapped_flush_shadow_bytes','g_perf_mapped_flush_shadow_misses','g_perf_pointer_replay_hash_cache_hits','g_perf_pointer_replay_hash_cache_misses','g_perf_emitter_header_hits','g_perf_emitter_header_misses','g_perf_rs_draws','g_perf_rs_fallbacks','g_perf_rs_handle_fallbacks','g_perf_rs_ui_draws','g_perf_rs_ui_fallbacks','g_perf_rs_upload_hits','g_perf_rs_upload_misses','g_perf_rs_program_query_skips','g_perf_rs_ubo_query_skips','g_perf_rs_ubo_query_fallbacks','g_bindless_state_replay_nv_range_updates','g_bindless_state_replay_nv_range_redundant']
    names += [n for n in ('g_perf_emitter_header_driver_reads','g_perf_emitter_header_driver_ticks') if n in syms]
    names += [n for n in ('g_perf_emitter_read_record_overflow',) if n in syms]
    qpc_frequency=C.c_longlong()
    k.QueryPerformanceFrequency(C.byref(qpc_frequency))
    def read(n):
        b=C.create_string_buffer(8); got=C.c_size_t(); addr=base+syms[n]
        if not k.ReadProcessMemory(h,addr,b,8,C.byref(got)) or got.value!=8: return None
        return int.from_bytes(b.raw,'little')
    out=a.output; out.parent.mkdir(parents=True,exist_ok=True); f=out.open('w',encoding='utf-8',buffering=1)
    record_fields=('sequence','buffer','offset','program','backing','mapped','cached_headers',
                   'pointer0','pointer1','self_relative','reads','ticks','first_frame','last_frame',
                   'upload_frame','upload_offset','upload_size','upload_pointer0','upload_pointer1',
                   'invalidate_frame','invalidate_offset','invalidate_size')
    record_format=struct.Struct('<'+'Q'*len(record_fields))
    record_symbol=syms.get('g_perf_emitter_read_records')
    record_file=out.with_name('emitter_reads.jsonl').open('w',encoding='utf-8',buffering=1) if record_symbol else None
    last_records={}
    start=time.monotonic(); previous_time=start; prev={n:read(n) or 0 for n in names}
    while time.monotonic()-start<a.seconds and not out.with_name('stop_capture').exists():
        time.sleep(a.poll_ms/1000); cur={n:read(n) for n in names}; now=time.monotonic()
        if any(value is None for value in cur.values()): break
        window=now-previous_time; previous_time=now
        row={'time':dt.datetime.now().astimezone().isoformat(),'elapsed_s':now-start,'window_s':window,'frames':cur['g_frame_count']-prev['g_frame_count'],'fps':(cur['g_frame_count']-prev['g_frame_count'])/window,'counters':{n:cur[n]-prev[n] for n in names},'totals':cur}
        row['qpc_frequency']=qpc_frequency.value
        f.write(json.dumps(row,separators=(',',':'))+'\n'); f.flush(); prev=cur
        if record_file and row['counters'].get('g_perf_emitter_header_driver_reads'):
            data=C.create_string_buffer(record_format.size*128); got=C.c_size_t()
            if k.ReadProcessMemory(h,base+record_symbol,data,len(data),C.byref(got)) and got.value==len(data):
                for index,values in enumerate(record_format.iter_unpack(data.raw)):
                    if values[0]&1 or not values[1] or last_records.get(index)==values: continue
                    sequence=C.c_ulonglong(); check_got=C.c_size_t()
                    if not k.ReadProcessMemory(h,base+record_symbol+index*record_format.size,C.byref(sequence),8,C.byref(check_got)) or check_got.value!=8 or sequence.value!=values[0]: continue
                    last_records[index]=values
                    record_file.write(json.dumps(dict(time=row['time'],slot=index,**dict(zip(record_fields,values))),separators=(',',':'))+'\n')
    f.close(); k.CloseHandle(h)
    if record_file: record_file.close()
if __name__=='__main__':
    p=argparse.ArgumentParser(); p.add_argument('--pid',type=int,required=True); p.add_argument('--modules',type=Path,required=True); p.add_argument('--renderer',type=Path,required=True); p.add_argument('--symbol-file',type=Path); p.add_argument('--seconds',type=float,default=600); p.add_argument('--poll-ms',type=int,default=250); p.add_argument('--output',type=Path,required=True); main(p.parse_args())
