/* Offline source submission and pointer ABI regression test. No GL context. */
#include "../shim.c"

static const char *audit_output_dir;
static unsigned char audit_pool[8192];
static uint64_t audit_headers[2][4];
static int audit_failures;
static unsigned audit_submissions;
static GLuint audit_driver_buffer[96];
static GLintptr audit_driver_offset[96];
static GLsizeiptr audit_driver_length[96];
static unsigned audit_bind_calls;

static void WINAPI audit_shader_source(GLuint shader, GLsizei count,
                                      const char *const *strings,
                                      const GLint *lengths)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/shader_%u_submitted.glsl",
             audit_output_dir, shader);
    FILE *f = fopen(path, "wb");
    if (!f) { audit_failures++; return; }
    int total = 0;
    for (int i = 0; i < count; i++) {
        int n = lengths && lengths[i] >= 0 ? lengths[i] : (int)strlen(strings[i]);
        if (fwrite(strings[i], 1, (size_t)n, f) != (size_t)n) audit_failures++;
        total += n;
    }
    fclose(f);
    audit_submissions++;
    printf("submitted shader=%u bytes=%d\n", shader, total);
}

static void WINAPI audit_read_buffer(GLuint buffer, GLintptr offset,
                                    GLsizeiptr size, void *out)
{
    const unsigned char *src = NULL;
    size_t available = 0;
    if (buffer == 629) { src = audit_pool; available = sizeof audit_pool; }
    if (buffer == 630 || buffer == 631) {
        src = (const unsigned char *)audit_headers[buffer - 630];
        available = sizeof audit_headers[0];
    }
    if (!src || offset < 0 || size < 0 ||
        (uint64_t)offset + (uint64_t)size > available) {
        fprintf(stderr, "unexpected read buffer=%u offset=%lld size=%lld\n",
                buffer, (long long)offset, (long long)size);
        audit_failures++;
        if (size > 0) memset(out, 0, (size_t)size);
        return;
    }
    memcpy(out, src + (size_t)offset, (size_t)size);
}

static void WINAPI audit_bind_range(GLenum target, GLuint index, GLuint buffer,
                                   GLintptr offset, GLsizeiptr size)
{
    if (target == GL_SHADER_STORAGE_BUFFER && index < 96) {
        audit_driver_buffer[index] = buffer;
        audit_driver_offset[index] = offset;
        audit_driver_length[index] = size;
        audit_bind_calls++;
    }
}

static void WINAPI audit_bind_base(GLenum target, GLuint index, GLuint buffer)
{
    audit_bind_range(target, index, buffer, 0, 0);
}

static void WINAPI audit_bind_ranges(GLenum target, GLuint first, GLsizei count,
                                    const GLuint *buffers, const GLintptr *offsets,
                                    const GLsizeiptr *sizes)
{
    for (GLsizei i = 0; i < count; i++)
        audit_bind_range(target, first + i, buffers[i], offsets[i], sizes[i]);
}

static int audit_translate(GLuint shader, GLenum type, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "cannot read %s\n", path); return 0; }
    fseek(f, 0, SEEK_END);
    long length = ftell(f);
    rewind(f);
    if (length <= 0 || length > 1024 * 1024) { fclose(f); return 0; }
    char *src = malloc((size_t)length + 1);
    if (!src) { fclose(f); return 0; }
    size_t got = fread(src, 1, (size_t)length, f);
    fclose(f);
    if (got != (size_t)length) { free(src); return 0; }
    src[length] = 0;
    GLint n = (GLint)length;
    unsigned before = audit_submissions;
    g_shader_types[shader] = type;
    glShaderSource_shim(shader, 1, (const char *const *)&src, &n);
    free(src);
    printf("metadata shader=%u members=%d nested=%d replaced=%d\n", shader,
           g_shader_ptrs[shader].count, g_shader_ptrs[shader].emitter_struct_count,
           g_shader_replaced[shader]);
    for (int i = 0; i < g_shader_ptrs[shader].count; i++) {
        ptr_member *p = &g_shader_ptrs[shader].m[i];
        printf("  member=%s ubo=%d byte_offset=%d ssbo=%d\n",
               p->name, p->ubo_binding, p->ubo_byte_offset, p->ssbo_binding);
    }
    return audit_submissions == before + 1 && !g_shader_replaced[shader];
}

static void audit_link_metadata(GLuint program, GLuint shader)
{
    /* These structs have identical fields. This reproduces the single-VS
       metadata copy in glLinkProgram_shim without running a driver linker. */
    _Static_assert(sizeof g_prog_ptrs[0] == sizeof g_shader_ptrs[0], "metadata size");
    memcpy(&g_prog_ptrs[program], &g_shader_ptrs[shader], sizeof g_prog_ptrs[0]);
    g_prog_shader_count[program] = 1;
    g_prog_shaders[program][0] = shader;
}

static void audit_expect(GLuint binding, GLuint buffer, GLuint offset)
{
    int ok = g_ssbo_buffer[binding] == buffer &&
             (!buffer || g_ssbo_offset[binding] == offset) &&
             audit_driver_buffer[binding] == buffer &&
             (!buffer || audit_driver_offset[binding] == offset) &&
             audit_driver_length[binding] == (GLsizeiptr)g_ssbo_length[binding];
    printf("  ssbo=%u actual=%u:%llu:%llu expected=%u:%u %s\n", binding,
           g_ssbo_buffer[binding], (unsigned long long)g_ssbo_offset[binding],
           (unsigned long long)g_ssbo_length[binding], buffer, offset,
           ok ? "PASS" : "FAIL");
    if (!ok) audit_failures++;
}

static void audit_case(const char *name, GLuint program, int pingpong, int null_mask)
{
    uint64_t *h = audit_headers[pingpong];
    h[0] = make_fake_addr(628, 0);
    h[1] = (null_mask & 1) ? 0 : make_fake_addr(629, pingpong ? 224 : 0);
    h[2] = (null_mask & 2) ? 0 : make_fake_addr(629, pingpong ? 0 : 224);
    h[3] = (null_mask & 4) ? 0 : make_fake_addr(629, 1024);
    g_buffer_write_serial[630 + pingpong]++;
    g_core_ubo_buffer[0] = 630 + pingpong;
    g_core_ubo_offset[0] = 0;
    g_core_ubo_length[0] = 32;
    g_core_ubo_bind_serial[0]++;
    g_current_program = program;
    g_perf_pointer_last_key_valid = 0;
    printf("case=%s program=%u pingpong=%d null_mask=%d\n", name, program,
           pingpong, null_mask);
    apply_pointer_bindings();
    audit_expect(30, 628, 0);
    audit_expect(31, (null_mask & 4) ? 0 : 629, 1024);
    if (g_prog_ptrs[program].emitter_struct_count) {
        audit_expect(32, (null_mask & 1) ? 0 : 629, pingpong ? 240 : 16);
        audit_expect(33, (null_mask & 1) ? 0 : 629, pingpong ? 244 : 20);
        audit_expect(34, (null_mask & 2) ? 0 : 629, pingpong ? 16 : 240);
        audit_expect(35, (null_mask & 2) ? 0 : 629, pingpong ? 20 : 244);
    }
    unsigned before = audit_bind_calls;
    apply_pointer_bindings();
    if (g_perf_pointer_ssbo_dedup_on && before != audit_bind_calls) {
        fprintf(stderr, "unchanged replay issued new binds\n");
        audit_failures++;
    }
}

static void audit_layout_guard(void)
{
    static const char *const variants[] = {
        "Particle* g_particles; Unused* g_unused0; Unused* g_unused1; float* m_randoms;",
        "// header\n Particle* g_particles; /* gap */ Unused* g_unused0; Unused* g_unused1; float* m_randoms;",
        "uint prefix; Particle* g_particles; Unused* g_unused0; Unused* g_unused1; float* m_randoms;",
        "Particle* g_particles; float* m_randoms; Unused* g_unused0; Unused* g_unused1;",
        "Particle* g_particles; Unused* g_unused0; Unused* g_unused1; float* m_randoms; uint tail;"
    };
    for (int i = 0; i < 5; i++) {
        int got = per_emitter_pointer_block_matches(variants[i], (int)strlen(variants[i]));
        int ok = got == (i < 2);
        printf("layout_variant=%d matched=%d %s\n", i, got, ok ? "PASS" : "FAIL");
        if (!ok) audit_failures++;
    }
}

int main(int argc, char **argv)
{
    if (argc != 2 && argc != 3) { fprintf(stderr, "usage: %s OUTPUT_DIR [FIXTURE_DIR]\n", argv[0]); return 2; }
    audit_output_dir = argv[1];
    g_telemetry_quiet = 1;
    g_pointer_ubo_layout_fix_on = 1;
    g_shader_compile_check_on = 0;
    g_perf_pointer_scan_key_on = 1;
    g_perf_pointer_scan_cache_v2_on = 1;
    g_perf_pointer_scan_stack_on = 1;
    g_perf_pointer_ubo_trim_on = 1;
    g_ssbo_dumped = 4;
    real_glShaderSource = audit_shader_source;
    real_glBindBufferRange = audit_bind_range;
    real_glBindBufferBase = audit_bind_base;
    real_glBindBuffersRange = audit_bind_ranges;
    real_glGetNamedBufferSubData = audit_read_buffer;
    const char *fixture_dir = argc == 3 ? argv[2] : "native-gacha-shaders-v1";
    const unsigned fixture_shaders[] = {2610, 2602, 2603};
    for (unsigned i = 0; i < 3; ++i) {
        char path[1024];
        snprintf(path, sizeof path, "%s/native_shader_%u.glsl", fixture_dir, fixture_shaders[i]);
        if (!audit_translate(fixture_shaders[i], i == 2 ? 0x8B30 : 0x8B31, path)) return 2;
    }
    audit_link_metadata(2611, 2610);
    audit_link_metadata(2604, 2602);
    g_buffer_size[628] = 148800;
    g_buffer_size[629] = sizeof audit_pool;
    g_buffer_size[630] = g_buffer_size[631] = 32;
    uint64_t list0[] = {make_fake_addr(629, 16), make_fake_addr(629, 20)};
    uint64_t list1[] = {make_fake_addr(629, 240), make_fake_addr(629, 244)};
    memcpy(audit_pool, list0, sizeof list0);
    memcpy(audit_pool + 224, list1, sizeof list1);
    audit_layout_guard();
    for (int optimized = 0; optimized < 2; optimized++) {
        g_perf_pointer_replay_fastpath_on = optimized;
        g_perf_pointer_replay_general_on = optimized;
        g_perf_pointer_replay_hash_cache_on = optimized;
        g_perf_pointer_replay_ssbo_key_on = optimized;
        g_perf_pointer_program_class_cache_on = optimized;
        g_perf_pointer_ssbo_batch_on = optimized;
        g_perf_pointer_ssbo_dedup_on = optimized;
        printf("optimized=%d\n", optimized);
        audit_case("producer-first", 2611, 0, 0);
        audit_case("producer-pingpong", 2611, 1, 0);
        audit_case("billboard", 2604, 0, 0);
        audit_case("null-unused0", 2611, 0, 1);
        audit_case("null-unused1", 2611, 0, 2);
        audit_case("null-randoms", 2611, 0, 4);
    }
    printf("RESULT failures=%d submissions=%u\n", audit_failures, audit_submissions);
    return audit_failures ? 1 : 0;
}
