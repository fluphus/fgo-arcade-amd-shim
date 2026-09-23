#include "../shim.c"

static unsigned driver_vao, driver_calls;
static int driver_interval;
static int driver_query_interval;
static BOOL driver_swap_result = TRUE;
static int driver_exposes_swap = 1;
static void WINAPI bind_noop(GLuint vao) { driver_vao = vao; driver_calls++; }
static BOOL WINAPI swap_interval(int interval)
{
    driver_interval = interval;
    return driver_swap_result;
}
static int WINAPI get_swap_interval(void) { return driver_query_interval; }
static PROC WINAPI fixture_entry(LPCSTR name)
{
    if (!strcmp(name, "wglSwapIntervalEXT"))
        return driver_exposes_swap ? (PROC)swap_interval : NULL;
    if (!strcmp(name, "wglGetSwapIntervalEXT"))
        return driver_exposes_swap ? (PROC)get_swap_interval : NULL;
    return !strcmp(name, "glBindVertexArray") ? (PROC)bind_noop : NULL;
}

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    HMODULE helper = LoadLibraryA(argv[1]);
    if (!helper) return 3;
    typedef void (WINAPI *restore_t)(glBindVertexArray_t);
    restore_t restore = (restore_t)GetProcAddress(helper, "restore_vao");
    if (!restore) return 4;
    LARGE_INTEGER frequency, start, end;
    QueryPerformanceFrequency(&frequency);
    g_telemetry_quiet = 1;
    real_wglGetProcAddress = fixture_entry;
    typedef BOOL (WINAPI *swap_t)(int);
    typedef int (WINAPI *get_swap_t)(void);
    swap_t swap = (swap_t)wglGetProcAddress_shim("wglSwapIntervalEXT");
    get_swap_t get = (get_swap_t)wglGetProcAddress_shim("wglGetSwapIntervalEXT");
    if (swap != wrap_wglSwapIntervalEXT || get != wrap_wglGetSwapIntervalEXT) return 5;
    for (int uncapped = 0; uncapped <= 1; ++uncapped) {
        g_no_vao_pacing_on = uncapped;
        for (int requested = -1; requested <= 2; ++requested) {
            int expected = requested == 1 ? 0 : requested;
            for (int result = 0; result <= 1; ++result) {
                driver_swap_result = result;
                if (swap(requested) != result || driver_interval != expected) return 6;
            }
        }
        for (int queried = -1; queried <= 2; ++queried) {
            driver_query_interval = queried;
            int expected = 0;
            if (get() != expected) return 7;
        }
    }
    g_no_vao_pacing_on = 0;
    driver_exposes_swap = 0;
    if (wglGetProcAddress_shim("wglSwapIntervalEXT") ||
        wglGetProcAddress_shim("wglGetSwapIntervalEXT")) return 8;
    driver_exposes_swap = 1;
    puts("release_swap_control: routing_cases=16, interval1-to0, zero query and unsupported extension passed");
    for (int external = 0; external <= 1; ++external) {
        wrap_glBindVertexArray(0);
        unsigned long long before = g_frame_count;
        unsigned calls_before = driver_calls;
        QueryPerformanceCounter(&start);
        for (int frame = 0; frame < 240; ++frame) {
            wrap_glBindVertexArray(1);
            wrap_glBindVertexArray(0);
            if (external) restore(wrap_glBindVertexArray);
        }
        QueryPerformanceCounter(&end);
        double seconds = (double)(end.QuadPart-start.QuadPart)/frequency.QuadPart;
        printf("external_restore=%d boundaries=%llu driver_calls=%u seconds=%.6f logical_fps=%.3f\n",
               external, g_frame_count-before, driver_calls-calls_before, seconds, 240.0/seconds);
        if (g_frame_count-before != 240 || driver_vao != 0 || g_current_vao != 0 ||
            driver_calls-calls_before != 480u*(external+1)) return 1;
        /* An accidentally retained 60 Hz limiter takes four seconds here. */
        if (seconds >= 2.0) return 9;
    }
    puts("RESULT failures=0");
    FreeLibrary(helper);
    return 0;
}
