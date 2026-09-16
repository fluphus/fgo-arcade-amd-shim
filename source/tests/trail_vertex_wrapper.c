#ifndef SHIM_TEST_SOURCE
#define SHIM_TEST_SOURCE "../shim.c"
#endif
#include SHIM_TEST_SOURCE

__declspec(dllexport) void TestTrailConfigure(HMODULE module, wglGetProcAddress_t resolve)
{
    g_real = module;
    real_wglGetProcAddress = resolve;
    g_telemetry_quiet = 1;
}

__declspec(dllexport) int TestTrailReplay(GLuint program, GLuint vertex,
    GLuint stale, int fixed, GLintptr core_offset, GLintptr nv_offset,
    GLuint uv_binding, GLuint relative, GLsizei stride)
{
    g_current_program = program;
    g_current_vao = 0;
    g_unified_attrib_state = 1;
    g_particle_vertex_binding_translation_on = fixed;
    g_bindless_state_replay_cache_on = 1;
    g_perf_pointer_replay_hash_cache_on = 1;
    g_model_vertex_stream_translation_on = 1;
    g_buffer_size[vertex] = g_buffer_size[stale] = 524288;
    for (GLuint a = 0; a < 16; a++) {
        if (a < 2) wrap_glEnableVertexArrayAttrib(0, a);
        else wrap_glDisableVertexArrayAttrib(0, a);
    }
    wrap_glVertexArrayVertexBuffer(0, 0, vertex, core_offset, stride);
    wrap_glVertexArrayVertexBuffer(0, 1, stale, core_offset, stride);
    wrap_glVertexArrayAttribBinding(0, 0, 0);
    wrap_glVertexArrayAttribBinding(0, 1, uv_binding);
    wrap_glVertexArrayAttribFormat(0, 0, 4, 0x1406, 0, 0);
    wrap_glVertexArrayAttribFormat(0, 1, 4, 0x1406, 0, relative);
    stub_glBufferAddressRangeNV(GL_VERTEX_ATTRIB_ARRAY_ADDRESS_NV, 0,
        make_fake_addr(vertex, (GLuint)nv_offset), 393216);
    stub_glBufferAddressRangeNV(GL_VERTEX_ATTRIB_ARRAY_ADDRESS_NV, 1,
        make_fake_addr(stale, 207536), 16384);
    apply_unified_attribs();
    return particle_vertex_binding_ready();
}

__declspec(dllexport) void TestTrailRepeat(void) { apply_unified_attribs(); }
