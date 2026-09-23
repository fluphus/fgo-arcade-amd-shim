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
    names=['g_frame_count','g_perf_pointer_exact_cache_hits','g_perf_pointer_exact_cache_misses','g_perf_pointer_shadow_reads','g_perf_cpu_shadow_reads','g_perf_mapped_flush_shadow_reads','g_perf_mapped_flush_shadow_flushes','g_perf_mapped_flush_shadow_bytes','g_perf_mapped_flush_shadow_misses','g_perf_pointer_replay_hash_cache_hits','g_perf_pointer_replay_hash_cache_misses','g_perf_emitter_header_hits','g_perf_emitter_header_misses','g_perf_rs_draws','g_perf_rs_fallbacks','g_perf_rs_handle_fallbacks','g_perf_rs_ui_draws','g_perf_rs_ui_fallbacks','g_perf_rs_upload_hits','g_perf_rs_upload_misses','g_perf_rs_program_query_skips','g_perf_rs_ubo_query_skips','g_perf_rs_ubo_query_fallbacks','g_bindless_state_replay_nv_range_updates','g_bindless_state_replay_nv_range_redundant','g_perf_nv_ea_bind_calls','g_perf_nv_ea_bind_skips']
    names += [n for n in ('g_perf_emitter_header_driver_reads','g_perf_emitter_header_driver_ticks') if n in syms]
    names += [n for n in ('g_perf_rs_batch_scopes','g_perf_rs_batch_draws') if n in syms]
    names += [n for n in ('g_submission_gpu_sample_frames','g_submission_gpu_unavailable',
                          'g_submission_gpu_disabled','g_submission_gpu_context_resets') if n in syms]
    names += [n for n in ('g_perf_emitter_read_record_overflow',) if n in syms]
    names += [n for n in ('g_perf_mapped_read_cache_hits',
                          'g_perf_mapped_read_cache_misses',
                          'g_perf_mapped_read_cache_bytes') if n in syms]
    names += [n for n in (
        'g_perf_pointer_fastpath_skips',
        'g_perf_pointer_ubo_binds',
        'g_perf_pointer_ssbo_binds',
        'g_perf_pointer_ssbo_unbinds',
        'g_perf_pointer_ssbo_skips',
        'g_perf_pointer_ssbo_batch_calls',
        'g_perf_pointer_ssbo_batch_slots',
        'g_perf_pointer_scan_hits',
        'g_perf_pointer_scan_misses',
        'g_perf_pointer_scan_replacements',
        'g_perf_pointer_scan_stack_reads',
        'g_perf_pointer_program_class_cache_hits',
        'g_perf_pointer_program_class_cache_misses',
        'g_perf_pointer_trimmed_reads',
        'g_perf_pointer_skinning_ubo_skips',
        'g_perf_pointer_matrix_ubo_skips',
        'g_perf_pointer_skinning_direct_ubo_binds',
        'g_bindless_state_replay_cache_hits',
        'g_bindless_state_replay_cache_misses') if n in syms]
    names += [n for n in (
        'g_perf_mapped_audit_reads','g_perf_mapped_audit_eligible',
        'g_perf_mapped_audit_hits','g_perf_mapped_audit_changed',
        'g_perf_mapped_audit_reusable_bytes','g_perf_mapped_audit_flushes',
        'g_perf_mapped_audit_boundaries','g_perf_mapped_audit_unflushed_hits',
        'g_perf_mapped_audit_collisions') if n in syms]
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
    mismatch_fields=('sequence','frame','program','buffer','offset','size',
                     'access','gpu_exposed','serial','first_diff','old_byte','new_byte')
    mismatch_format=struct.Struct('<'+'Q'*len(mismatch_fields))
    mismatch_symbol=syms.get('g_perf_mapped_audit_mismatches')
    mismatch_file=out.with_name('mapped_mismatches.jsonl').open('w',encoding='utf-8',buffering=1) if mismatch_symbol else None
    timeline_fields=('sequence','frame','thread','hdc','path','interval_begin',
                     'swap_begin','swap_end','first_render','boundary_begin','boundary_end',
                     'boundary_calls','finish_ticks','flush_ticks','client_wait_ticks',
                     'server_wait_ticks','finish_calls','flush_calls','client_wait_calls',
                     'server_wait_calls','client_wait_result','sequence_end')
    timeline_format=struct.Struct('<'+'Q'*len(timeline_fields))
    timeline_symbol=syms.get('g_present_timeline_records')
    timeline_file=out.with_name('present_timeline.jsonl').open('w',encoding='utf-8',buffering=1) if timeline_symbol is not None else None
    timeline_next=max(1,(read('g_present_timeline_sequence') or 0)-4096+1) if timeline_file else 1
    timeline_first=timeline_next
    timeline_lost=0
    def read_blob(address,size):
        data=C.create_string_buffer(size); got=C.c_size_t()
        if not k.ReadProcessMemory(h,address,data,size,C.byref(got)) or got.value!=size: return None
        return data.raw
    def drain_timeline():
        nonlocal timeline_next,timeline_lost
        if not timeline_file: return
        committed=read('g_present_timeline_sequence')
        if committed is None: return
        oldest=max(1,committed-4096+1)
        if timeline_next<oldest:
            timeline_lost+=oldest-timeline_next
            timeline_next=oldest
        while timeline_next<=committed:
            slot=(timeline_next-1)%4096
            count=min(committed-timeline_next+1,4096-slot)
            data=read_blob(base+timeline_symbol+slot*timeline_format.size,count*timeline_format.size)
            if data is None: return
            for values in timeline_format.iter_unpack(data):
                if values[0]!=timeline_next or values[-1]!=timeline_next: return
                timeline_file.write(json.dumps(dict(zip(timeline_fields,values)),separators=(',',':'))+'\n')
                timeline_next+=1
        timeline_file.flush()
    seen_mismatches=set()
    gpu_fields=('sequence','frame','program','framebuffer','kind','draws','cpu_begin','cpu_end',
                'gpu_begin','gpu_end','swap_begin','swap_end','truncated','sequence_end')
    gpu_format=struct.Struct('<'+'Q'*len(gpu_fields))
    gpu_symbol=syms.get('g_submission_gpu_records')
    gpu_file=out.with_name('submission_gpu.jsonl').open('w',encoding='utf-8',buffering=1) if gpu_symbol is not None else None
    gpu_next=max(1,(read('g_submission_gpu_sequence') or 0)-4096+1) if gpu_file else 1
    gpu_lost=0
    def drain_gpu():
        nonlocal gpu_next,gpu_lost
        if not gpu_file: return
        committed=read('g_submission_gpu_sequence')
        if committed is None: return
        oldest=max(1,committed-4096+1)
        if gpu_next<oldest:
            gpu_lost+=oldest-gpu_next; gpu_next=oldest
        while gpu_next<=committed:
            slot=(gpu_next-1)%4096
            count=min(committed-gpu_next+1,4096-slot)
            data=read_blob(base+gpu_symbol+slot*gpu_format.size,count*gpu_format.size)
            if data is None: return
            for values in gpu_format.iter_unpack(data):
                if values[0]!=gpu_next or values[-1]!=gpu_next: return
                gpu_file.write(json.dumps(dict(zip(gpu_fields,values)),separators=(',',':'))+'\n')
                gpu_next+=1
        gpu_file.flush()
    start=time.monotonic(); previous_time=start; prev={n:read(n) or 0 for n in names}
    while time.monotonic()-start<a.seconds and not out.with_name('stop_capture').exists():
        time.sleep(a.poll_ms/1000); cur={n:read(n) for n in names}; now=time.monotonic()
        if any(value is None for value in cur.values()): break
        window=now-previous_time; previous_time=now
        row={'time':dt.datetime.now().astimezone().isoformat(),'elapsed_s':now-start,'window_s':window,'frames':cur['g_frame_count']-prev['g_frame_count'],'fps':(cur['g_frame_count']-prev['g_frame_count'])/window,'counters':{n:cur[n]-prev[n] for n in names},'totals':cur}
        row['qpc_frequency']=qpc_frequency.value
        f.write(json.dumps(row,separators=(',',':'))+'\n'); f.flush(); prev=cur
        drain_timeline()
        drain_gpu()
        if mismatch_file and len(seen_mismatches)<min(32,cur.get('g_perf_mapped_audit_changed',0)):
            data=C.create_string_buffer(mismatch_format.size*32); got=C.c_size_t()
            if k.ReadProcessMemory(h,base+mismatch_symbol,data,len(data),C.byref(got)) and got.value==len(data):
                for index,values in enumerate(mismatch_format.iter_unpack(data.raw)):
                    if values[0]!=2 or index in seen_mismatches: continue
                    seen_mismatches.add(index)
                    mismatch_file.write(json.dumps(dict(time=row['time'],slot=index,**dict(zip(mismatch_fields,values))),separators=(',',':'))+'\n')
        if record_file and row['counters'].get('g_perf_emitter_header_driver_reads'):
            data=C.create_string_buffer(record_format.size*128); got=C.c_size_t()
            if k.ReadProcessMemory(h,base+record_symbol,data,len(data),C.byref(got)) and got.value==len(data):
                for index,values in enumerate(record_format.iter_unpack(data.raw)):
                    if values[0]&1 or not values[1] or last_records.get(index)==values: continue
                    sequence=C.c_ulonglong(); check_got=C.c_size_t()
                    if not k.ReadProcessMemory(h,base+record_symbol+index*record_format.size,C.byref(sequence),8,C.byref(check_got)) or check_got.value!=8 or sequence.value!=values[0]: continue
                    last_records[index]=values
                    record_file.write(json.dumps(dict(time=row['time'],slot=index,**dict(zip(record_fields,values))),separators=(',',':'))+'\n')
    drain_timeline()
    drain_gpu()
    if gpu_file:
        gpu_file.close()
        out.with_name('submission_gpu_status.json').write_text(json.dumps({
            'records_through':gpu_next-1,'overwritten_records':gpu_lost,
            'qpc_frequency':qpc_frequency.value}),encoding='utf-8')
    if timeline_file:
        timeline_file.close()
        out.with_name('timeline_status.json').write_text(json.dumps({
            'records_from':timeline_first,'records_through':timeline_next-1,
            'overwritten_records':timeline_lost,
            'other_thread_swaps':read('g_present_timeline_other_swaps'),
            'qpc_frequency':qpc_frequency.value}),encoding='utf-8')
    f.close(); k.CloseHandle(h)
    if record_file: record_file.close()
    if mismatch_file: mismatch_file.close()
if __name__=='__main__':
    p=argparse.ArgumentParser(); p.add_argument('--pid',type=int,required=True); p.add_argument('--modules',type=Path,required=True); p.add_argument('--renderer',type=Path,required=True); p.add_argument('--symbol-file',type=Path); p.add_argument('--seconds',type=float,default=600); p.add_argument('--poll-ms',type=int,default=250); p.add_argument('--output',type=Path,required=True); main(p.parse_args())
