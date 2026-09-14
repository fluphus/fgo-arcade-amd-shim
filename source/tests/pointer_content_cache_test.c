/* Exercise actual pointer replay with CPU-owned headers and a mock driver. */
#ifndef POINTER_CONTENT_CACHE_EMBEDDED
#include "../shim.c"
#endif

enum { HEADER = 61000, DATA_A = 61001, DATA_B = 61002, PROGRAM = 61003 };
static unsigned char header[4096];
static GLuint driver_ssbo[96], driver_ubo[64];
static GLintptr driver_ssbo_off[96], driver_ubo_off[64];
static GLsizeiptr driver_ssbo_len[96], driver_ubo_len[64];
static unsigned reads, read_bytes, binds, failures, draws;
static uint64_t mutations = 1469598103934665603ULL;
static uint64_t states = 1469598103934665603ULL;

static void require(int ok, const char *what)
{
    if (!ok) { fprintf(stderr, "FAIL draw=%u %s\n", draws, what); failures++; }
}

static void WINAPI bind_range(GLenum target, GLuint index, GLuint buffer,
                               GLintptr offset, GLsizeiptr size)
{
    binds++;
    mutations = dataflow_hash_u64(mutations, target);
    mutations = dataflow_hash_u64(mutations, index);
    mutations = dataflow_hash_u64(mutations, buffer);
    mutations = dataflow_hash_u64(mutations, (GLuint64)offset);
    mutations = dataflow_hash_u64(mutations, (GLuint64)size);
    if (target == GL_SHADER_STORAGE_BUFFER && index < 96) {
        driver_ssbo[index] = buffer;
        driver_ssbo_off[index] = offset;
        driver_ssbo_len[index] = size;
    } else if (target == GL_UNIFORM_BUFFER && index < 64) {
        driver_ubo[index] = buffer;
        driver_ubo_off[index] = offset;
        driver_ubo_len[index] = size;
    } else require(0, "binding target/index");
}
static void WINAPI bind_base(GLenum target, GLuint index, GLuint buffer)
{ bind_range(target, index, buffer, 0, 0); }
static void WINAPI bind_ranges(GLenum target, GLuint first, GLsizei count,
                                const GLuint *buffers, const GLintptr *offsets,
                                const GLsizeiptr *sizes)
{
    for (GLsizei i = 0; i < count; i++)
        bind_range(target, first + i, buffers[i], offsets[i], sizes[i]);
}
static void WINAPI read_buffer(GLuint buffer, GLintptr offset,
                                GLsizeiptr length, void *out)
{
    reads++;
    read_bytes += (unsigned)length;
    require(buffer == HEADER && offset >= 0 && length >= 0 &&
            offset + length <= sizeof header, "driver read bounds");
    if (!failures) memcpy(out, header + offset, (size_t)length);
}
static void WINAPI link_noop(GLuint program) { (void)program; }
static PROC WINAPI no_entry(LPCSTR name) { (void)name; return NULL; }

static void layout(int first, int second, int swap_bindings)
{
    const GLuint shader = PROGRAM + 1;
    memset(&g_shader_ptrs[shader], 0, sizeof g_shader_ptrs[shader]);
    g_shader_ptrs[shader].count = 2;
    const char *names[] = {"g_lights", "g_reflection_proxies"};
    for (int i = 0; i < 2; i++) {
        ptr_member *p = &g_shader_ptrs[shader].m[i];
        p->ubo_binding = 0;
        p->ssbo_binding = 30 + (swap_bindings ? 1 - i : i);
        p->ubo_byte_offset = i ? second : first;
        strcpy(p->name, names[i]);
    }
    g_prog_shader_count[PROGRAM] = 1;
    g_prog_shaders[PROGRAM][0] = shader;
    wrap_glLinkProgram(PROGRAM);
    /* Use a fresh reference scan when the program metadata is replaced. */
    g_ubo_scan_n = 0;
    g_perf_pointer_last_key_valid = 0;
}

static void put(int base, int at, uint64_t value)
{ memcpy(header + base + at, &value, 8); }

static void draw(int base, int first, int second, uint64_t a, uint64_t b)
{
    put(base, first, a);
    put(base, second, b);
    header[base + 512]++;
    trace_buf_write(HEADER);
    perf_cpu_shadow_subdata(HEADER, base, 1024, header + base);
    trace_core_ubo_bind(0, HEADER, base, 1024);
    g_current_program = PROGRAM;
    apply_pointer_bindings();
    for (unsigned i = 0; i < 96; i++) {
        require(driver_ssbo[i] == g_ssbo_buffer[i] &&
                driver_ssbo_off[i] == (GLintptr)g_ssbo_offset[i] &&
                driver_ssbo_len[i] == (GLsizeiptr)g_ssbo_length[i], "SSBO mirror");
        states = dataflow_hash_u64(states, driver_ssbo[i]);
        states = dataflow_hash_u64(states, (GLuint64)driver_ssbo_off[i]);
        states = dataflow_hash_u64(states, (GLuint64)driver_ssbo_len[i]);
    }
    for (unsigned i = 0; i < 64; i++) {
        states = dataflow_hash_u64(states, driver_ubo[i]);
        states = dataflow_hash_u64(states, (GLuint64)driver_ubo_off[i]);
        states = dataflow_hash_u64(states, (GLuint64)driver_ubo_len[i]);
    }
    draws++;
}

int main(void)
{
    real_wglGetProcAddress = no_entry;
    real_glLinkProgram = link_noop;
    real_glBindBufferRange = bind_range;
    real_glBindBufferBase = bind_base;
    real_glBindBuffersRange = bind_ranges;
    real_glGetNamedBufferSubData = read_buffer;
    g_telemetry_quiet = 1;
    g_perf_timing_on = 1;
    g_perf_pointer_scan_key_on = g_perf_pointer_scan_cache_v2_on = 1;
    g_perf_pointer_scan_stack_on = g_perf_pointer_ubo_trim_on = 1;
    g_perf_pointer_ssbo_batch_on = g_perf_pointer_ssbo_dedup_on = 1;
    g_perf_pointer_shadow_on = g_perf_cpu_shadow_on = 1;
    g_ssbo_dumped = 4;
    g_buffer_size[HEADER] = sizeof header;
    g_buffer_size[DATA_A] = g_buffer_size[DATA_B] = 65536;
#ifndef POINTER_CONTENT_CACHE_REFERENCE
    g_perf_pointer_exact_cache_on = 1;
#endif
    g_bindless_maps[0] = (bindless_map_rec){
        .buffer=HEADER, .offset=0, .length=sizeof header, .access=0x52,
        .ptr=header, .active=1};
    layout(840, 848, 0);
    uint64_t a = make_fake_addr(DATA_A, 32), b = make_fake_addr(DATA_B, 256);
    for (int i = 0; i < 900; i++) draw((i % 3) * 1024, 840, 848, a, b);
    require(driver_ssbo[30] == DATA_A && driver_ssbo_off[30] == 32 &&
            driver_ssbo[31] == DATA_B && driver_ssbo_off[31] == 256,
            "captured PerScene member offsets");
    for (int i = 0; i < 300; i++)
        draw((i % 3) * 1024, 840, 848, make_fake_addr(DATA_A, 32 + (i % 8) * 16),
             i % 5 ? b : 0);
    g_buffer_size[DATA_B] = 70000;
    draw(0, 840, 848, a, b);
    require(driver_ssbo_len[31] == 70000 - 256, "target buffer resize");
    draw(1024, 840, 848, a, 0x123456789abcdef0ULL);
    draw(2048, 840, 848, a, make_fake_addr(DATA_B, 80000));
    require(driver_ssbo_len[31] == 0, "target range beyond backing");
    layout(840, 848, 1);
    draw(0, 840, 848, a, b);
    require(driver_ssbo[31] == DATA_A && driver_ssbo[30] == DATA_B,
            "relink changes binding metadata");
    layout(16, 24, 0);
    for (int i = 0; i < 512; i++)
        draw((i % 3) * 1024, 16, 24, make_fake_addr(DATA_A, i * 16), b);

    g_bindless_maps[0].active = 0;
    perf_cpu_shadow_replace(HEADER, sizeof header, header);
    for (int i = 0; i < 60; i++) draw((i % 3) * 1024, 16, 24, a, b);
    require(reads == 0, "CPU-owned source avoids driver reads");
    perf_cpu_shadow_invalidate(HEADER);
    unsigned before = reads;
    for (int i = 0; i < 5; i++) draw((i % 3) * 1024, 16, 24, a, b);
    require(reads - before == 5, "GPU-only source keeps readback fallback");

    g_bindless_maps[0].active = 1;
    layout(16, 400, 0);
    draw(0, 16, 400, a, b);
#ifndef POINTER_CONTENT_CACHE_REFERENCE
    require(perf_pointer_exact_lookup(PROGRAM, 0, 0, 2, HEADER, 0, 1024) == NULL,
            "wide pointer span bypass");
    layout(16, 24, 0);
    g_prog_ptrs[PROGRAM].emitter_struct_count = 1;
    require(perf_pointer_exact_lookup(PROGRAM, 0, 0, 2, HEADER, 0, 1024) == NULL,
            "nested emitter bypass");
    g_prog_ptrs[PROGRAM].emitter_struct_count = 0;
    g_prog_ptrs[PROGRAM].needs_emitter_count = 1;
    require(perf_pointer_exact_lookup(PROGRAM, 0, 0, 2, HEADER, 0, 1024) == NULL,
            "emitter count bypass");
    g_prog_ptrs[PROGRAM].needs_emitter_count = 0;
    g_prog_ptrs[PROGRAM].m[1].ubo_byte_offset = -1;
    require(perf_pointer_exact_lookup(PROGRAM, 0, 0, 2, HEADER, 0, 1024) == NULL,
            "unknown layout bypass");
    require(g_perf_pointer_exact_cache_hits > 1100, "content cache reused across rebinds");
    unsigned long long hits = g_perf_pointer_exact_cache_hits;
    unsigned long long misses = g_perf_pointer_exact_cache_misses;
#else
    unsigned long long hits = 0, misses = 0;
#endif
    printf("{\"draws\":%u,\"failures\":%u,\"binds\":%u,\"mutations\":\"%016llx\","
           "\"states\":\"%016llx\",\"driver_reads\":%u,\"driver_bytes\":%u,"
           "\"legacy_scan_misses\":%llu,\"content_hits\":%llu,\"content_misses\":%llu}\n",
           draws,failures,binds,mutations,states,reads,read_bytes,
           g_perf_pointer_scan_misses,hits,misses);
    return failures ? 1 : 0;
}
