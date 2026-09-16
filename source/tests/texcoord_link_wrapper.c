#ifndef SHIM_TEST_SOURCE
#define SHIM_TEST_SOURCE "../shim.c"
#endif
#define DllMain texcoord_fixture_unused_dllmain
#include SHIM_TEST_SOURCE

__declspec(dllexport) void TestTexcoordSetup(HMODULE module,
                                            wglGetProcAddress_t resolve)
{
    g_real = module;
    real_wglGetProcAddress = resolve;
    g_log_on = 0;
    g_telemetry_quiet = 1;
}

__declspec(dllexport) void TestTexcoordTrack(GLuint shader, GLenum type,
                                            const char *source)
{
    g_shader_types[shader] = type;
    store_shader_src(shader, source, (int)strlen(source));
}

__declspec(dllexport) void TestTexcoordAttach(GLuint program, GLuint shader)
{
    wrap_glAttachShader(program, shader);
}

__declspec(dllexport) void TestTexcoordLink(GLuint program)
{
    wrap_glLinkProgram(program);
}

__declspec(dllexport) const char *TestTexcoordSource(GLuint shader)
{
    return g_shader_src[shader].src;
}
