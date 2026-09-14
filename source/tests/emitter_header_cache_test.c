#define main legacy_emitter_main
#include "per_emitter_pointer_test.c"
#undef main

_Static_assert(sizeof(perf_emitter_read_record) == 22 * sizeof(uint64_t), "external readback record layout");

static unsigned nested_reads;
static void WINAPI counted_read(GLuint b, GLintptr off, GLsizeiptr size, void *data)
{
    if (b == 629 && size == 16) nested_reads++;
    audit_read_buffer(b, off, size, data);
}

static void WINAPI upload(GLuint b, GLintptr off, GLsizeiptr size, const void *data)
{
    if (b == 629 && data && off >= 0 && size >= 0 && off + size <= sizeof audit_pool)
        memmove(audit_pool + off, data, (size_t)size);
}
static void WINAPI upload_bound(GLenum target, GLintptr off, GLsizeiptr size, const void *data)
{ upload(g_bound_buffer[target], off, size, data); }
static void WINAPI allocate(GLuint b, GLsizeiptr size, const void *data, GLenum flags)
{ (void)flags; upload(b, 0, size, data); }
static void WINAPI allocate_bound(GLenum target, GLsizeiptr size, const void *data, GLenum flags)
{ allocate(g_bound_buffer[target], size, data, flags); }
static void * WINAPI map_named(GLuint b, GLenum access)
{ (void)b; (void)access; return audit_pool; }
static void * WINAPI map_range(GLuint b, GLintptr off, GLsizeiptr size, GLbitfield flags)
{ (void)b; (void)size; (void)flags; return audit_pool + off; }
static GLboolean WINAPI unmap(GLuint b) { (void)b; return 1; }
static void WINAPI copy_named(GLuint r, GLuint w, GLintptr ro, GLintptr wo, GLsizeiptr n)
{ (void)r; (void)w; (void)ro; (void)wo; (void)n; }
static void WINAPI delete_buffers(GLsizei n, const GLuint *b) { (void)n; (void)b; }
static BOOL WINAPI make_current(HDC dc, HGLRC rc) { (void)dc; (void)rc; return TRUE; }
static BOOL WINAPI delete_context(HGLRC rc) { (void)rc; return TRUE; }
static PROC WINAPI resolve(const char *name)
{
    if (!strcmp(name, "glNamedBufferSubData")) return (PROC)upload;
    if (!strcmp(name, "glBufferSubData")) return (PROC)upload_bound;
    if (!strcmp(name, "glNamedBufferData") || !strcmp(name, "glNamedBufferStorage")) return (PROC)allocate;
    if (!strcmp(name, "glBufferData") || !strcmp(name, "glBufferStorage")) return (PROC)allocate_bound;
    if (!strcmp(name, "glMapBuffer") || !strcmp(name, "glMapNamedBuffer")) return (PROC)map_named;
    if (!strcmp(name, "glMapBufferRange") || !strcmp(name, "glMapNamedBufferRange")) return (PROC)map_range;
    if (!strcmp(name, "glUnmapBuffer") || !strcmp(name, "glUnmapNamedBuffer")) return (PROC)unmap;
    if (!strcmp(name, "glCopyNamedBufferSubData") || !strcmp(name, "glCopyBufferSubData")) return (PROC)copy_named;
    if (!strcmp(name, "glDeleteBuffers")) return (PROC)delete_buffers;
    return NULL;
}

static void check(int ok, const char *name)
{ if (!ok) { fprintf(stderr, "FAIL %s\n", name); audit_failures++; } }

static void refresh(void)
{
    uint64_t pair0[] = {make_fake_addr(629, 16), make_fake_addr(629, 20)};
    uint64_t pair1[] = {make_fake_addr(629, 240), make_fake_addr(629, 244)};
    memcpy(audit_pool, pair0, 16); memcpy(audit_pool + 224, pair1, 16);
    wrap_glNamedBufferSubData(629, 0, sizeof audit_pool, audit_pool);
}

static uint64_t replay(int enabled, unsigned header_count, int gpu_initialized, unsigned *reads)
{
    g_perf_emitter_header_cache_on = enabled;
    memset(audit_pool, 0, sizeof audit_pool);
    if (gpu_initialized) wrap_glNamedBufferData(629, sizeof audit_pool, NULL, 0);
    for (unsigned i = 0; i < header_count; i++) {
        uint64_t pair[] = {make_fake_addr(629, 224*i + 16), make_fake_addr(629, 224*i + 20)};
        memcpy(audit_pool + 224*i, pair, sizeof pair);
    }
    if (!gpu_initialized) wrap_glNamedBufferSubData(629, 0, sizeof audit_pool, audit_pool);
    nested_reads = 0;
    unsigned long long driver_before = g_perf_emitter_header_driver_reads;
    uint64_t hash = 1469598103934665603ULL;
    for (unsigned frame = 0; frame < 256; frame++) {
        int pingpong = frame & 1;
        unsigned first = 224 * (frame % header_count);
        unsigned second = 224 * ((frame + 1) % header_count);
        audit_headers[pingpong][0] = make_fake_addr(628, 0);
        audit_headers[pingpong][1] = make_fake_addr(629, first);
        audit_headers[pingpong][2] = make_fake_addr(629, second);
        audit_headers[pingpong][3] = make_fake_addr(629, 1024);
        trace_buf_write(630 + pingpong);
        trace_core_ubo_bind(0, 630 + pingpong, 0, 32);
        /* Emulate GPU count/index stores without altering the uploaded pointers. */
        memcpy(audit_pool + first + 16, &frame, 4); memcpy(audit_pool + first + 20, &frame, 4);
        memcpy(audit_pool + second + 16, &frame, 4); memcpy(audit_pool + second + 20, &frame, 4);
        g_current_program = 2611;
        g_perf_pointer_last_key_valid = 0;
        apply_pointer_bindings();
        check(audit_driver_buffer[32] == 629 && audit_driver_offset[32] == first + 16, "count binding");
        check(audit_driver_buffer[33] == 629 && audit_driver_offset[33] == first + 20, "index binding");
        for (unsigned s = 30; s < 36; s++) {
            hash = dataflow_hash_u64(hash, audit_driver_buffer[s]);
            hash = dataflow_hash_u64(hash, audit_driver_offset[s]);
            hash = dataflow_hash_u64(hash, audit_driver_length[s]);
        }
    }
    *reads = nested_reads;
    check(g_perf_emitter_header_driver_reads - driver_before == nested_reads,
          "aggregate readback counter matches driver calls");
    return hash;
}

int main(int argc, char **argv)
{
    if (legacy_emitter_main(argc, argv)) return 1;
    real_wglGetProcAddress = resolve; real_glGetNamedBufferSubData = counted_read;
    real_wglMakeCurrent = make_current; real_wglDeleteContext = delete_context;
    g_perf_regular_samplers_on = 1;
    perf_rs_upload_gpu_target(0x90D2, 629);
    unsigned before, after;
    uint64_t h0 = replay(0, 2, 0, &before), h1 = replay(1, 2, 0, &after);
    check(h0 == h1 && before == 512 && after == 0, "actual replay/read reduction");
    unsigned char out[16];
    const GLintptr offsets[] = {0, 7, 8, 15, 16, 20, 223, 224, 231, 232, 239, 240};
    for (unsigned i = 0; i < sizeof offsets / sizeof offsets[0]; i++) {
        refresh();
        unsigned char byte = audit_pool[offsets[i]];
        wrap_glNamedBufferSubData(629, offsets[i], 1, &byte);
        check(perf_emitter_header_copy(629, 0, out) == (offsets[i] >= 16), "first overlap");
        check(perf_emitter_header_copy(629, 224, out) == !(offsets[i] >= 224 && offsets[i] < 240), "second overlap");
    }
    g_bound_buffer[GL_ARRAY_BUFFER] = 629;
    g_bound_buffer[0x8f36] = 628; g_bound_buffer[0x8f37] = 629;
    refresh();
    wrap_glCopyNamedBufferSubData(628, 629, 0, 16, 4);
    check(perf_emitter_header_copy(629, 0, out), "count copy preserves pointers");
    wrap_glCopyBufferSubData(0x8f36, 0x8f37, 0, 20, 4);
    check(perf_emitter_header_copy(629, 0, out), "index copy preserves pointers");
    for (int action = 0; action < 12; action++) {
        refresh();
        switch (action) {
        case 0: wrap_glNamedBufferData(629, sizeof audit_pool, NULL, 0); break;
        case 1: wrap_glBufferData(GL_ARRAY_BUFFER, sizeof audit_pool, NULL, 0); break;
        case 2: wrap_glNamedBufferStorage(629, sizeof audit_pool, NULL, 0); break;
        case 3: wrap_glBufferStorage(GL_ARRAY_BUFFER, sizeof audit_pool, NULL, 0); break;
        case 4: wrap_glCopyNamedBufferSubData(628, 629, 0, 0, 16); break;
        case 5: wrap_glCopyBufferSubData(0x8f36, 0x8f37, 0, 0, 16); break;
        case 6: wrap_glMapNamedBuffer(629, 0x88b9); break;
        case 7: wrap_glMapBuffer(GL_ARRAY_BUFFER, 0x88ba); break;
        case 8: wrap_glMapNamedBufferRange(629, 0, 32, 2); break;
        case 9: wrap_glMapBufferRange(GL_ARRAY_BUFFER, 0, 32, 2); break;
        case 10: { GLuint b=629; wrap_glDeleteBuffers(1, &b); break; }
        case 11: wglMakeCurrent_shim(NULL, (HGLRC)(uintptr_t)17); break;
        }
        check(!perf_emitter_header_copy(629, 0, out), "lifetime invalidation");
        memset(g_bindless_maps, 0, sizeof g_bindless_maps);
    }
    refresh(); wglDeleteContext_shim((HGLRC)(uintptr_t)17);
    check(!perf_emitter_header_copy(629, 0, out), "context deletion");
    for (int action = 0; action < 4; action++) {
        refresh();
        switch (action) {
        case 0: perf_emitter_clear_buffer(GL_ARRAY_BUFFER, 0, 0, 0, NULL); break;
        case 1: perf_emitter_clear_named_buffer(629, 0, 0, 0, NULL); break;
        case 2: perf_emitter_clear_buffer_range(GL_ARRAY_BUFFER, 0, 8, 1, 0, 0, NULL); break;
        case 3: perf_emitter_clear_named_buffer_range(629, 0, 8, 1, 0, 0, NULL); break;
        }
        check(!perf_emitter_header_copy(629, 0, out), "clear invalidation");
    }
    for (GLuint k = 0; k < 8; k++) {
        GLuint b = 629 + 128*k; g_buffer_size[b] = 64;
        uint64_t pairs[] = {make_fake_addr(b, 16), make_fake_addr(b, 20)};
        perf_emitter_header_upload(b, 0, 16, pairs);
    }
    for (GLuint k = 0; k < 8; k++) {
        GLuint b = 629 + 128*k;
        int hit = perf_emitter_header_copy(b, 0, out);
        check(hit, "separate buffers retain headers");
        if (hit) {
            uint64_t expected[] = {make_fake_addr(b, 16), make_fake_addr(b, 20)};
            check(!memcmp(out, expected, 16), "collision cannot false-hit");
        }
    }
    g_buffer_size[629] = sizeof audit_pool;
    refresh();
    uint64_t bad[] = {make_fake_addr(628, 16), make_fake_addr(628, 20)};
    wrap_glNamedBufferSubData(629, 0, 16, bad);
    check(!perf_emitter_header_copy(629, 0, out), "other layout fallback");
    printf("EMITTER_HEADER before_reads=%u after_reads=%u state=%llx/%llx failures=%d\n",
           before, after, (unsigned long long)h0, (unsigned long long)h1, audit_failures);
    perf_rs_upload_gpu_target(0x90D2, 629);
    h0 = replay(0, 32, 0, &before); h1 = replay(1, 32, 0, &after);
    check(h0 == h1 && before == 512 && after == 0, "32-header pool read reduction");
    printf("EMITTER_POOL headers=32 before_reads=%u after_reads=%u state=%llx/%llx failures=%d\n",
           before, after, (unsigned long long)h0, (unsigned long long)h1, audit_failures);
    wrap_glCopyNamedBufferSubData(628, 629, 0, 224*7 + 8, 224*3);
    for (unsigned i = 0; i < 32; i++)
        check(perf_emitter_header_copy(629, 224*i, out) == (i < 7 || i > 10),
              "range invalidates only overlapping headers");
    const unsigned order[] = {10, 7, 9, 8};
    for (unsigned i = 0; i < sizeof order / sizeof order[0]; i++)
        wrap_glNamedBufferSubData(629, 224*order[i], 16, audit_pool + 224*order[i]);
    for (unsigned i = 0; i < 32; i++) {
        int hit = perf_emitter_header_copy(629, 224*i, out);
        check(hit && !memcmp(out, audit_pool + 224*i, 16), "out-of-order reupload retains exact bytes");
    }
    { GLuint b = 629; wrap_glDeleteBuffers(1, &b); }
    for (unsigned i = 0; i < 32; i++)
        check(!perf_emitter_header_copy(629, 224*i, out), "delete invalidates every pool header");
    perf_emitter_header_context_change((HGLRC)(uintptr_t)18);
    unsigned recorded = 0;
    for (unsigned i = 0; i < PERF_EMITTER_READ_RECORD_CAP; i++) {
        const perf_emitter_read_record *record = &g_perf_emitter_read_records[i];
        if (!record->buffer) continue;
        check(!(record->sequence & 1) && record->reads > 0, "complete readback record");
        check(record->pointers[0] == make_fake_addr((GLuint)record->buffer, (GLuint)record->offset + 16) &&
              record->pointers[1] == make_fake_addr((GLuint)record->buffer, (GLuint)record->offset + 20),
              "diagnostic preserves observed pointer bytes");
        recorded++;
    }
    check(recorded >= 32 && g_perf_emitter_read_record_overflow == 0, "readback identity coverage");
    printf("EMITTER_POOL_LIFETIME failures=%d\n", audit_failures);
    perf_rs_upload_gpu_target(0x90D2, 629);
    h0 = replay(0, 2, 1, &before); h1 = replay(1, 2, 1, &after);
    check(h0 == h1 && before == 512 && after == 2, "GPU-initialized header first-read admission");
    printf("EMITTER_GPU_HEADER before_reads=%u after_reads=%u state=%llx/%llx failures=%d\n",
           before, after, (unsigned long long)h0, (unsigned long long)h1, audit_failures);
    wrap_glNamedBufferSubData(629, 0, 16, bad);
    nested_reads = 0;
    for (unsigned draw = 0; draw < 16; draw++) {
        audit_headers[0][1] = make_fake_addr(629, 0);
        audit_headers[0][2] = make_fake_addr(629, 224);
        trace_buf_write(630); trace_core_ubo_bind(0, 630, 0, 32);
        g_perf_pointer_last_key_valid = 0;
        apply_pointer_bindings();
        check(audit_driver_buffer[32] == 628 && audit_driver_offset[32] == 16 &&
              audit_driver_buffer[33] == 628 && audit_driver_offset[33] == 20,
              "unknown header layout keeps actual pointer targets");
    }
    check(nested_reads == 16, "unknown GPU header is never cached");
    perf_emitter_header_context_change((HGLRC)(uintptr_t)19);
    printf("EMITTER_GPU_FALLBACK failures=%d\n", audit_failures);
    return audit_failures ? 1 : 0;
}
