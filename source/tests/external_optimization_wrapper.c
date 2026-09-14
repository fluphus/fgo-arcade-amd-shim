#include "regular_sampler_wrapper.c"

static char *cache_submitted;
static int cache_submitted_len;
static unsigned cache_submits;
static GLuint cache_receiver;

static void WINAPI cache_capture(GLuint shader, GLsizei count,
                                  const char *const *text, const GLint *length)
{
    if (count != 1 || !text || !text[0]) abort();
    int size = length && length[0] >= 0 ? length[0] : (int)strlen(text[0]);
    free(cache_submitted);
    cache_submitted = malloc((size_t)size + 1);
    if (!cache_submitted) abort();
    memcpy(cache_submitted, text[0], size);
    cache_submitted[size] = 0;
    cache_submitted_len = size;
    cache_receiver = shader;
    cache_submits++;
}

__declspec(dllexport) int TestCacheSubmit(GLuint shader, GLenum type,
    const char *source, GLint length, int enabled)
{
    g_shader_types[shader] = type;
    g_shader_cache_on = enabled;
    real_glShaderSource = cache_capture;
    cache_submits = 0;
    glShaderSource_shim(shader, 1, &source, &length);
    return cache_submits == 1 && cache_receiver == shader ? cache_submitted_len : -1;
}
__declspec(dllexport) const char *TestCacheOutput(void) { return cache_submitted; }
__declspec(dllexport) int TestCacheMetadata(GLuint shader, void *out)
{
    unsigned char *p = out;
    if (p) {
        memcpy(p, &g_shader_ptrs[shader], sizeof g_shader_ptrs[shader]);
        p += sizeof g_shader_ptrs[shader];
        memcpy(p, &g_shader_replaced[shader], sizeof g_shader_replaced[shader]);
        p += sizeof g_shader_replaced[shader];
        *p++ = g_shader_had_nv_pointer[shader];
        *p = g_shader_particle_stage[shader];
    }
    return sizeof g_shader_ptrs[shader] + sizeof g_shader_replaced[shader] + 2;
}
__declspec(dllexport) const char *TestCachePath(GLuint shader, GLenum type,
    const char *source, GLint length)
{
    static char path[MAX_PATH * 4];
    unsigned long long a, b;
    g_shader_cache_on = 1;
    if (length < 0) length = (GLint)strlen(source);
    return shader_cache_path(shader, type, source, length, path, sizeof path, &a, &b) ? path : NULL;
}
__declspec(dllexport) int TestCacheHit(GLuint shader, GLenum type,
    const char *source, GLint length)
{
    g_shader_cache_on = 1;
    g_shader_types[shader] = type;
    real_glShaderSource = cache_capture;
    cache_submits = 0;
    if (length < 0) length = (GLint)strlen(source);
    int hit = shader_cache_try_load(shader, type, source, length);
    return hit && cache_submits == 1 && cache_receiver == shader;
}
__declspec(dllexport) void TestCacheDelete(GLuint shader)
{
    real_glDeleteShader = NULL;
    glDeleteShader_shim(shader);
}

static unsigned diagnostic_draws, diagnostic_reads, diagnostic_selects, diagnostic_bad_args;
static GLint diagnostic_read_buffer = 0x0405;
static void WINAPI diagnostic_multi(GLenum mode, const GLint *first,
    const GLsizei *count, GLsizei draws)
{
    diagnostic_draws++;
    if (mode != 4 || draws != 2 || first[0] != 0 || first[1] != 3 ||
        count[0] != 3 || count[1] != 6) diagnostic_bad_args++;
}
static void WINAPI diagnostic_read(GLint x, GLint y, GLsizei w, GLsizei h,
    GLenum format, GLenum type, void *data)
{ (void)x; (void)y; (void)format; (void)type; diagnostic_reads++; memset(data, 0, w*h*3); }
static void WINAPI diagnostic_select(GLenum buffer)
{ diagnostic_selects++; diagnostic_read_buffer = buffer; }
static void WINAPI diagnostic_get(GLenum token, GLint *out)
{ *out = token == 0x0c02 ? diagnostic_read_buffer : 0; }
static PROC WINAPI diagnostic_resolve(LPCSTR name)
{
    if (!strcmp(name, "glMultiDrawArrays")) return (PROC)diagnostic_multi;
    if (!strcmp(name, "glReadPixels")) return (PROC)diagnostic_read;
    if (!strcmp(name, "glReadBuffer")) return (PROC)diagnostic_select;
    if (!strcmp(name, "glGetIntegerv")) return (PROC)diagnostic_get;
    return NULL;
}
__declspec(dllexport) void TestDiagnosticDraw(unsigned *result)
{
    real_wglGetProcAddress = diagnostic_resolve;
    g_current_program = 200;
    g_perf_regular_samplers_on = 0;
    GLint first[] = {0, 3}; GLsizei count[] = {3, 6};
    for (unsigned i = 0; i < 2000; i++) wrap_glMultiDrawArrays(4, first, count, 2);
    result[0] = diagnostic_draws; result[1] = diagnostic_reads;
    result[2] = diagnostic_selects; result[3] = diagnostic_bad_args;
    result[4] = diagnostic_read_buffer;
}

/* Exact previous waiter, for an offline CPU/deadline comparison. */
static void legacy_pace(void)
{
    static LARGE_INTEGER freq, last;
    LARGE_INTEGER now;
    if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&now);
    if (last.QuadPart && freq.QuadPart > 0) {
        const LONGLONG target = freq.QuadPart / 60;
        LONGLONG remaining = target - (now.QuadPart - last.QuadPart);
        if (remaining > 0) {
            DWORD ms = (DWORD)(remaining * 1000 / freq.QuadPart);
            if (ms > 1) Sleep(ms - 1);
            do { QueryPerformanceCounter(&now); } while (now.QuadPart - last.QuadPart < target);
        }
    }
    last = now;
}
static unsigned long long thread_time(void)
{
    FILETIME created, exited, kernel, user;
    GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user);
    return (((unsigned long long)kernel.dwHighDateTime << 32) | kernel.dwLowDateTime) +
           (((unsigned long long)user.dwHighDateTime << 32) | user.dwLowDateTime);
}
__declspec(dllexport) void TestPacing(int legacy, int frames, double work_ms,
    double *intervals, double *result)
{
    LARGE_INTEGER freq, begin, now, previous;
    QueryPerformanceFrequency(&freq);
    void (*pace)(void) = legacy ? legacy_pace : pace_frame_60hz;
    pace();
    QueryPerformanceCounter(&previous); begin = previous;
    unsigned long long cpu = thread_time();
    ULONG64 cycles_before = 0, cycles_after = 0;
    QueryThreadCycleTime(GetCurrentThread(), &cycles_before);
    for (int i = 0; i < frames; i++) {
        if (work_ms > 0) {
            LONGLONG end = previous.QuadPart + (LONGLONG)(work_ms * freq.QuadPart / 1000.0);
            do { YieldProcessor(); QueryPerformanceCounter(&now); } while (now.QuadPart < end);
        }
        pace(); QueryPerformanceCounter(&now);
        intervals[i] = 1000.0 * (now.QuadPart - previous.QuadPart) / freq.QuadPart;
        previous = now;
    }
    result[0] = (thread_time() - cpu) / 10000.0;
    result[1] = 1000.0 * (now.QuadPart - begin.QuadPart) / freq.QuadPart;
    result[2] = pace_timer() != NULL;
    QueryThreadCycleTime(GetCurrentThread(), &cycles_after);
    result[3] = (double)(cycles_after - cycles_before);
}
