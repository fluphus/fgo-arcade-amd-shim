#include "regular_sampler_wrapper.c"

__declspec(dllexport) void TestMappedCacheSetup(HMODULE real_module,
                                               wglGetProcAddress_t resolve)
{
    g_real=real_module; real_wglGetProcAddress=resolve; g_telemetry_quiet=1;
    g_perf_pointer_shadow_on=g_perf_regular_samplers_on=1;
    g_perf_mapped_read_cache_on=1;
}
__declspec(dllexport) void *TestMappedCacheMap(GLuint buffer, GLsizeiptr size,
                                              GLbitfield access)
{
    wrap_glNamedBufferStorage(buffer,size,NULL,(access&0xc3u)|0x100u);
    return wrap_glMapNamedBufferRange(buffer,0,size,access);
}
__declspec(dllexport) int TestMappedCacheRead(GLuint buffer, GLintptr offset,
                                            GLsizeiptr size, void *out)
{
    bindless_map_rec *m=bindless_map_find(buffer);
    if (!m) return 0;
    perf_rs_wc_request r[PERF_RS_SLOTS+1]; int count=0;
    if (perf_rs_queue_mapped_read(m,offset,size,out,r,&count)) {
        if (count) perf_rs_wc_read_batch(r,count);
        return 1;
    }
    return perf_rs_mapped_copy(m,buffer,offset,size,out);
}
__declspec(dllexport) void TestMappedCacheBoundary(unsigned kind, GLuint buffer,
                                                  GLsync sync)
{
    switch (kind) {
    case 0: g_frame_count++; break;
    case 1: wrap_glFlushMappedNamedBufferRange(buffer,0,4096); break;
    case 2: wrap_glBindBuffer(GL_ARRAY_BUFFER,buffer);
            wrap_glFlushMappedBufferRange(GL_ARRAY_BUFFER,0,4096); break;
    case 3: wrap_glMemoryBarrier(0x4004); break;
    case 4: wrap_glMemoryBarrierByRegion(4); break;
    case 5: wrap_glFinish(); break;
    case 6: wrap_glFlush(); break;
    case 7: wrap_glClientWaitSync(sync,1,1000000000ULL); break;
    case 8: wrap_glWaitSync(sync,0,~0ULL); break;
    case 9: perf_mapped_flush_shadow_context_reset(); break;
    }
}
__declspec(dllexport) void TestMappedCacheWrite(unsigned kind, GLuint buffer,
                                               GLuint source, const void *data)
{
    switch (kind) {
    case 0: wrap_glNamedBufferSubData(buffer,64,48,data); break;
    case 1: wrap_glCopyNamedBufferSubData(source,buffer,0,64,48); break;
    case 2: wrap_glBindBuffer(0x8F36,source); wrap_glBindBuffer(0x8F37,buffer);
            wrap_glCopyBufferSubData(0x8F36,0x8F37,0,64,48); break;
    case 3: perf_emitter_clear_named_buffer_range(buffer,0x8236,64,48,0x8D94,0x1405,data); break;
    case 4: wrap_glBindBuffer(GL_ARRAY_BUFFER,buffer);
            perf_emitter_clear_buffer_range(GL_ARRAY_BUFFER,0x8236,64,48,0x8D94,0x1405,data); break;
    case 5: perf_emitter_clear_named_buffer(buffer,0x8236,0x8D94,0x1405,data); break;
    case 6: wrap_glBindBuffer(GL_ARRAY_BUFFER,buffer);
            perf_emitter_clear_buffer(GL_ARRAY_BUFFER,0x8236,0x8D94,0x1405,data); break;
    }
}
__declspec(dllexport) void TestMappedCacheRelease(GLuint buffer, int erase)
{
    if (erase) wrap_glDeleteBuffers(1,&buffer);
    else wrap_glUnmapNamedBuffer(buffer);
}
__declspec(dllexport) void *TestMappedCacheRemap(GLuint buffer)
{ return wrap_glMapNamedBufferRange(buffer,0,4096,0x52); }
__declspec(dllexport) uint64_t TestMappedCacheCounter(unsigned which)
{
    if (which==0) return g_perf_mapped_read_cache_hits;
    if (which==1) return g_perf_mapped_read_cache_misses;
    return g_perf_mapped_read_cache_bytes;
}
__declspec(dllexport) double TestMappedCacheBench(GLuint buffer, unsigned count,
    int enabled, unsigned period, uint64_t *sum)
{
    LARGE_INTEGER freq,begin,end; unsigned char bytes[52];
    bindless_map_rec *m=bindless_map_find(buffer);
    if (!m) return -1;
    g_perf_mapped_read_cache_on=enabled;
    perf_mapped_read_boundary();
    *sum=0;
    QueryPerformanceFrequency(&freq);QueryPerformanceCounter(&begin);
    for (unsigned i=0;i<count;i++) {
        if (period && i%period==0) perf_mapped_read_boundary();
        perf_rs_wc_request r[2];int n=0;
        if (!perf_rs_queue_mapped_read(m,0,4,bytes,r,&n) ||
            !perf_rs_queue_mapped_read(m,64,48,bytes+4,r,&n)) return -2;
        if (n) perf_rs_wc_read_batch(r,n);
        *sum+=bytes[i%4]+bytes[4+i%48];
    }
    QueryPerformanceCounter(&end);
    g_perf_mapped_read_cache_on=1;
    return (end.QuadPart-begin.QuadPart)*1000.0/freq.QuadPart;
}
