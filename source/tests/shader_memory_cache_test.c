#include "../shim.c"

static unsigned int submits;
static unsigned long long last_hash;
static void WINAPI source_sink(GLuint shader, GLsizei count,
    const char *const *strings, const GLint *lengths)
{
    (void)shader;
    if (count != 1 || !strings || !strings[0] || !lengths) abort();
    ++submits;
    last_hash = dataflow_hash_bytes((const unsigned char *)strings[0], lengths[0]);
}
static void check(int success, const char *message)
{
    if (!success) { fprintf(stderr, "FAIL: %s\n", message); exit(1); }
}
static double benchmark(GLuint shader, const char *source, int len, int memory)
{
    LARGE_INTEGER begin, end, frequency;
    QueryPerformanceFrequency(&frequency);
    g_shader_memory_cache_on = memory;
    QueryPerformanceCounter(&begin);
    for (int i = 0; i < 2000; ++i) glShaderSource_shim(shader, 1, &source, &len);
    QueryPerformanceCounter(&end);
    return 1000.0 * (end.QuadPart - begin.QuadPart) / frequency.QuadPart;
}
int main(void)
{
    const char *source = "#version 450 core\nvoid main(){gl_Position=vec4(0.0);}";
    const char *output = "#version 450 core\nvoid main(){gl_Position=vec4(1.0);}";
    unsigned long long ha, hb;
    const GLuint first = 65000, second = 64999;
    char path[MAX_PATH * 4];
    int len = (int)strlen(source);
    g_self_module = GetModuleHandleA(NULL);
    g_shader_cache_on = 1; g_telemetry_quiet = 1;
    real_glShaderSource = source_sink;
    g_shader_types[first] = g_shader_types[second] = 0x8B31;
    check(shader_cache_path(first, 0x8B31, source, len, path, sizeof path, &ha, &hb), "cache path");
    g_shader_ptrs[first].count = 1;
    g_shader_ptrs[first].m[0].ubo_binding = 7;
    g_shader_replaced[first] = 1; g_shader_had_nv_pointer[first] = 1;
    shader_memory_cache_remember(first, 0x8B31, source, len, output, strlen(output), ha, hb);
    check(shader_cache_try_load(second, 0x8B31, source, len), "different shader ID hits");
    check(last_hash == dataflow_hash_bytes((const unsigned char *)output, strlen(output)), "exact final source");
    check(g_shader_ptrs[second].count == 1 && g_shader_ptrs[second].m[0].ubo_binding == 7 &&
        g_shader_replaced[second] && g_shader_had_nv_pointer[second], "metadata restored");
    check(!shader_memory_cache_find(0x8B30, source, len, ha, hb), "stage isolated");
    char different[128]; strcpy(different, source); different[len - 2] ^= 1;
    check(!shader_memory_cache_find(0x8B31, different, len, ha, hb), "full bytes disambiguate equal hashes");
    g_shader_compile_check_on = 1;
    check(!shader_cache_try_load(second, 0x8B31, source, len), "diagnostic policy bypass");
    g_shader_compile_check_on = 0;
    for (unsigned i = 0; i < SHADER_MEMORY_CACHE_ENTRIES; ++i) shader_memory_cache_drop(&g_shader_memory_cache[i]);
    check(!g_shader_memory_cache_bytes, "released accounting");
    DeleteFileA(path);
    glShaderSource_shim(first, 1, &source, &len);
    unsigned long long expected = last_hash;
    double disk_ms = benchmark(second, source, len, 0);
    check(last_hash == expected, "disk submission matches cold lowering");
    unsigned long long reads = g_shader_cache_disk_reads;
    double memory_ms = benchmark(second, source, len, 1);
    check(last_hash == expected && reads == g_shader_cache_disk_reads && g_shader_memory_cache_hits >= 2000,
        "warm memory path has identical submission and zero file reads");
    char *large = malloc(100000); check(large != NULL, "budget fixture");
    memset(large, ' ', 100000);
    for (unsigned i = 0; i < 160; ++i) {
        large[0] = (char)i; large[1] = (char)(i >> 8);
        shader_memory_cache_remember(first, 0x8B31, large, 100000, large, 100000, i, i);
        check(g_shader_memory_cache_bytes <= SHADER_MEMORY_CACHE_BYTES, "LRU byte limit");
    }
    free(large);
    printf("PASS: exact bytes/metadata, policy, stage, collision, LRU budget; 2000 submits disk=%.3f ms memory=%.3f ms\n", disk_ms, memory_ms);
    return 0;
}
