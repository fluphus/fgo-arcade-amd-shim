#ifndef SHIM_TEST_SOURCE
#define SHIM_TEST_SOURCE "../shim.c"
#endif
#include SHIM_TEST_SOURCE

__declspec(dllexport) void TestCastleConfigure(HMODULE module, wglGetProcAddress_t resolve)
{
    g_real = module;
    real_wglGetProcAddress = resolve;
    g_telemetry_quiet = 1;
}

__declspec(dllexport) int TestCastleReplay(GLuint program, GLuint vertex,
    int enabled, GLintptr core_offset, GLintptr nv_offset, int repeat)
{
    if (!repeat) {
        g_current_program = program;
        g_current_vao = 0;
        g_unified_attrib_state = 1;
        g_particle_vertex_binding_translation_on = enabled;
        g_bindless_state_replay_cache_on = 1;
        g_perf_pointer_replay_hash_cache_on = 1;
        g_model_vertex_stream_translation_on = 1;
        g_buffer_size[vertex] = 524288;
        for (GLuint a = 0; a < 16; a++) {
            if (0x9f & (1u << a)) wrap_glEnableVertexArrayAttrib(0, a);
            else wrap_glDisableVertexArrayAttrib(0, a);
            wrap_glVertexArrayAttribBinding(0, a, a >= 3 ? 1 : 0);
        }
        wrap_glVertexArrayVertexBuffer(0, 0, vertex, core_offset, 32);
        wrap_glVertexArrayVertexBuffer(0, 1, vertex, core_offset, 32);
        wrap_glVertexArrayAttribFormat(0, 0, 3, 0x1406, 0, 0);
        wrap_glVertexArrayAttribFormat(0, 1, 4, 0x8d9f, 1, 12);
        wrap_glVertexArrayAttribFormat(0, 2, 4, 0x8d9f, 1, 16);
        wrap_glVertexArrayAttribFormat(0, 3, 2, 0x1403, 0, 20);
        wrap_glVertexArrayAttribFormat(0, 4, 2, 0x1403, 0, 24);
        wrap_glVertexArrayAttribFormat(0, 7, 4, 0x1401, 1, 28);
        stub_glBufferAddressRangeNV(GL_VERTEX_ATTRIB_ARRAY_ADDRESS_NV, 0,
            make_fake_addr(vertex, (GLuint)nv_offset), 78688);
        stub_glBufferAddressRangeNV(GL_VERTEX_ATTRIB_ARRAY_ADDRESS_NV, 1,
            make_fake_addr(vertex, (GLuint)nv_offset), 78688);
    }
    apply_unified_attribs();
    return particle_vertex_binding_ready();
}
