#include "regular_sampler_wrapper.c"

__declspec(dllexport) PROC TestScopeProc(const char *name) { return find_hook(name); }
__declspec(dllexport) void TestScopeClose(void) { perf_rs_close_pending(); }
__declspec(dllexport) void TestScopeEnable(int enabled)
{ perf_rs_close_pending(); g_perf_rs_lazy_on=enabled; }
__declspec(dllexport) int TestScopeDraw(GLuint program, unsigned count)
{
    glDrawArrays_t draw=(glDrawArrays_t)perf_rs_proc("glDrawArrays");
    g_current_program=program;
    int hits=0;
    for (unsigned i=0;i<count;i++) {
        unsigned long long start=0;
        perf_ordinary_sampler_begin(&start);
        hits+=perf_rs_begin(); draw(4,0,3); perf_rs_end();
    }
    return hits;
}
__declspec(dllexport) int TestScopeActive(void) { return g_perf_rs_lazy_batch; }
__declspec(dllexport) void TestScopeCounters(uint64_t *out)
{ out[0]=g_perf_rs_lazy_scopes; out[1]=g_perf_rs_lazy_hits; out[2]=g_perf_rs_lazy_restores; }
__declspec(dllexport) double TestScopeMeasure(GLuint program, unsigned count, int enabled)
{
    typedef void (WINAPI *finish_t)(void);
    finish_t finish=(finish_t)perf_rs_proc("glFinish");
    TestScopeEnable(enabled); TestRegularSeedState(program);
    LARGE_INTEGER a,b,hz;
    QueryPerformanceFrequency(&hz); finish(); QueryPerformanceCounter(&a);
    int hits=TestScopeDraw(program,count);
    perf_rs_close_pending(); finish(); QueryPerformanceCounter(&b);
    return hits==(int)count ? 1000.0*(b.QuadPart-a.QuadPart)/hz.QuadPart : -1;
}

__declspec(dllexport) int TestTileInstall(HMODULE module,wglGetProcAddress_t resolve,
                                       GLuint program,const char *source)
{
    g_real=module; real_wglGetProcAddress=resolve; g_telemetry_quiet=1;
    g_prog_shader_count[program]=1;
    g_prog_shaders[program][0]=59990;
    g_shader_types[59990]=0x8B31;
    store_shader_src(59990,source,(int)strlen(source));
    perf_tile_link(program); g_perf_rs_draws=1;
    return g_perf_tile_programs[program]!=NULL;
}
__declspec(dllexport) void TestTileUniform(GLuint program,const float *values)
{ perf_rs_program4fv(program,0,11,values); }
__declspec(dllexport) void TestTileSampler(GLuint program,GLint location,GLint unit)
{ perf_rs_program1i(program,location,unit); }
__declspec(dllexport) int TestTileDraw(GLuint program,int enabled)
{
    g_current_program=program; g_current_program_valid=1; g_perf_tile_on=enabled;
    g_perf_rs_gl.use(program);
    uint64_t before=g_perf_tile_draws;
    wrap_glDrawArrays(0,0,1152);
    return (int)(g_perf_tile_draws-before);
}
