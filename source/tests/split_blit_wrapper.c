#define DllMain split_blit_unused_DllMain
#include "../shim.c"
#undef DllMain

__declspec(dllexport) void TestSplitConfigure(HMODULE module, wglGetProcAddress_t resolve)
{
    g_real = module;
    real_wglGetProcAddress = resolve;
    g_telemetry_quiet = 1;
    g_perf_depth_copy_gpu_on = 1;
    g_p700_fbo105_sc_blit_split_on = 1;
}

__declspec(dllexport) void TestSplit(GLuint read_fbo, GLuint draw_fbo,
    GLuint source_depth, GLuint destination_depth)
{
    g_bound_read_framebuffer = read_fbo;
    g_bound_draw_framebuffer = draw_fbo;
    g_fbo_depth_attach[read_fbo] = source_depth;
    g_fbo_depth_attach[draw_fbo] = destination_depth;
    glBlitFramebuffer_t blit = (glBlitFramebuffer_t)trace_resolve("glBlitFramebuffer");
    p700_fbo105_sc_blit_apply(blit, 2, 3, 18, 15, 4, 5, 20, 17, 0x4100, 0x2600);
}
