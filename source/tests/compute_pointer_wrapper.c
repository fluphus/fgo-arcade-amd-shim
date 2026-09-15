#ifndef SHIM_TEST_SOURCE
#define SHIM_TEST_SOURCE "../shim.c"
#endif
#define DllMain compute_fixture_unused_dllmain
#include SHIM_TEST_SOURCE

__declspec(dllexport) void TestComputeSetup(HMODULE module,wglGetProcAddress_t resolve)
{
    g_real=module;real_wglGetProcAddress=resolve;g_log_on=0;g_telemetry_quiet=1;
}
__declspec(dllexport) void TestComputeTrack(GLuint program,GLuint shader,const char *source,int pointers)
{
    g_shader_types[shader]=0x91b9;store_shader_src(shader,source,(int)strlen(source));
    g_prog_shader_count[program]=1;g_prog_shaders[program][0]=shader;
    /* Exact offsets recovered from live program529's translation metadata. */
    g_prog_ptrs[program].count=pointers?2:0;
    if(pointers) {
        ptr_member *p=g_prog_ptrs[program].m;
        p[0].ubo_binding=p[1].ubo_binding=0;
        p[0].ssbo_binding=30;p[1].ssbo_binding=31;
        p[0].ubo_byte_offset=840;p[1].ubo_byte_offset=848;
        strcpy(p[0].name,"g_lights");strcpy(p[0].type,"Light");
        strcpy(p[1].name,"g_reflection_proxies");strcpy(p[1].type,"ReflectionProxy");
    }
}
__declspec(dllexport) void TestComputeCache(int on)
{
    g_perf_pointer_exact_cache_on=g_perf_pointer_ssbo_dedup_on=on;
    g_perf_pointer_scan_key_on=g_perf_pointer_scan_cache_v2_on=on;
    g_perf_pointer_replay_fastpath_on=g_perf_pointer_replay_general_on=on;
    g_perf_pointer_replay_ssbo_key_on=g_perf_pointer_ssbo_batch_on=on;
    g_perf_pointer_program_class_cache_on=on;
}
__declspec(dllexport) uint64_t TestComputeAddress(GLuint b,GLuint offset)
{ return make_fake_addr(b,offset); }
__declspec(dllexport) void TestComputeData(GLuint b,GLsizeiptr n,const void *p)
{ wrap_glNamedBufferData(b,n,p,0x88e8); }
__declspec(dllexport) void TestComputeSubData(GLuint b,GLintptr offset,GLsizeiptr n,const void *p)
{ wrap_glNamedBufferSubData(b,offset,n,p); }
__declspec(dllexport) void TestComputeBind(GLenum target,GLuint index,GLuint b,GLintptr off,GLsizeiptr n)
{ wrap_glBindBufferRange(target,index,b,off,n); }
__declspec(dllexport) void TestComputeUse(GLuint p) { wrap_glUseProgram(p); }
__declspec(dllexport) void TestComputeDispatch(GLuint x,GLuint y,GLuint z)
{ wrap_glDispatchCompute(x,y,z); }
