/* Bound GPU completion around the resource-only interval observed before TDR. */
static int g_battle_resource_boundary_on, g_battle_resource_failed;
static HGLRC g_battle_resource_context;
static DWORD g_battle_resource_thread;
static SRWLOCK g_battle_resource_lock = SRWLOCK_INIT;

static unsigned battle_resource_kind(const char *event)
{
    if (!strcmp(event, "tex_storage2")) return 1;
    if (!strcmp(event, "tex_upload2")) return 2;
    if (!strcmp(event, "tex_compressed2")) return 3;
    if (!strcmp(event, "tex_storage3")) return 4;
    if (!strcmp(event, "tex_upload3")) return 5;
    if (!strcmp(event, "tex_compressed3")) return 6;
    if (!strcmp(event, "tex_residency")) return 7;
    if (!strcmp(event, "present")) return 8;
    if (!strcmp(event, "present_front_blit")) return 9;
    return 0;
}

/* Match the native reference before the pre-fence; these queries change no GL state. */
static void battle_present_describe(HDC dc)
{
    battle_aux_snapshot();
    uint64_t swap_id = (uint64_t)(uintptr_t)dc;
    battle_progress_event("bpres_begin", 0, (int)swap_id, (int)(swap_id >> 32), 0, 0);
    HMODULE provider = GetModuleHandleA("opengl32real.dll");
    HDC (WINAPI *current_dc)(void) = provider ?
        (void *)GetProcAddress(provider, "wglGetCurrentDC") : NULL;
    uint64_t actual_dc = (uint64_t)(uintptr_t)(current_dc ? current_dc() : NULL);
    uint64_t context = (uint64_t)(uintptr_t)(battle_current_context ? battle_current_context() : NULL);
    battle_progress_event("bpres_dc", GetObjectType(dc), (int)actual_dc,
        (int)(actual_dc >> 32), (int)context, (int)(context >> 32));
    HWND window = WindowFromDC(dc);
    RECT client = {0}, rect = {0};
    unsigned status = window && GetClientRect(window, &client) ? 1u : 0u;
    if (window && GetWindowRect(window, &rect)) status |= 2u;
    uint64_t window_id = (uint64_t)(uintptr_t)window;
    battle_progress_event("bpres_window", status, (int)window_id, (int)(window_id >> 32),
        IsWindowVisible(window), IsIconic(window));
    battle_progress_event("bpres_client", status, client.left, client.top, client.right, client.bottom);
    battle_progress_event("bpres_rect", status, rect.left, rect.top, rect.right, rect.bottom);
    uint64_t style = (uint64_t)GetWindowLongPtrA(window, GWL_STYLE);
    uint64_t exstyle = (uint64_t)GetWindowLongPtrA(window, GWL_EXSTYLE);
    battle_progress_event("bpres_style", 0, (int)style, (int)(style >> 32),
        (int)exstyle, (int)(exstyle >> 32));
    PIXELFORMATDESCRIPTOR pfd = {0};
    int pf = GetPixelFormat(dc);
    int described = pf && DescribePixelFormat(dc, pf, sizeof pfd, &pfd);
    battle_progress_event("bpres_pfd", (GLenum)pf, (int)pfd.dwFlags,
        pfd.cColorBits, pfd.cAlphaBits, pfd.cDepthBits);
    battle_progress_event("bpres_pfd_aux", (GLenum)described, pfd.cStencilBits,
        pfd.cAuxBuffers, pfd.iPixelType, pfd.iLayerType);
    BOOL (WINAPI *attributes)(HDC, int, int, UINT, const int *, int *) =
        (void *)battle_frame_proc("wglGetPixelFormatAttribivARB");
    if (attributes && pf) {
        const int names[] = {0x2007, 0x2003, 0x2001, 0x2010};
        int values[4] = {0};
        BOOL ok = attributes(dc, pf, 0, 4, names, values);
        battle_progress_event("bpres_swap_method", (GLenum)ok, values[0], values[1], values[2], values[3]);
    } else {
        battle_progress_event("bpres_swap_missing", 0, 0, 0, 0, 0);
    }
    MONITORINFOEXA monitor = {0}; monitor.cbSize = sizeof monitor;
    DEVMODEA mode = {0}; mode.dmSize = sizeof mode;
    BOOL display_ok = GetMonitorInfoA(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST),
        (MONITORINFO *)&monitor) && EnumDisplaySettingsA(monitor.szDevice, ENUM_CURRENT_SETTINGS, &mode);
    battle_progress_event("bpres_display", (GLenum)display_ok, mode.dmPelsWidth,
        mode.dmPelsHeight, mode.dmDisplayFrequency, 0);
    void (WINAPI *get)(GLenum, GLint *) = (void *)battle_frame_proc("glGetIntegerv");
    if (!get) {
        battle_progress_event("bpres_gl_missing", 0, 0, 0, 0, 0);
        return;
    }
    GLint program = -1, draw = -1, read = -1, draw_buffer = -1, read_buffer = -1;
    GLint viewport[4] = {0}, samples = -1, sample_buffers = -1, double_buffer = -1, stereo = -1;
    GLint srgb = -1, scissor = -1, flags = -1, reset = -1;
    get(0x8b8d, &program); get(0x8ca6, &draw); get(0x8caa, &read);
    get(0x0c01, &draw_buffer); get(0x0c02, &read_buffer); get(0x0ba2, viewport);
    get(0x80a9, &samples); get(0x80a8, &sample_buffers);
    get(0x0c32, &double_buffer); get(0x0c33, &stereo);
    get(0x8db9, &srgb); get(0x0c11, &scissor); get(0x821e, &flags); get(0x8256, &reset);
    int (WINAPI *interval)(void) = (void *)battle_frame_proc("wglGetSwapIntervalEXT");
    GLint swap_interval = interval ? interval() : -999;
    battle_progress_event("bpres_state", (GLenum)program, draw, read, draw_buffer, read_buffer);
    battle_progress_event("bpres_viewport", 0, viewport[0], viewport[1], viewport[2], viewport[3]);
    battle_progress_event("bpres_samples", (GLenum)swap_interval, sample_buffers, samples, double_buffer, stereo);
    battle_progress_event("bpres_flags", 0, srgb, scissor, flags, reset);
    battle_progress_event("bpres_end", 0, 0, 0, 0, 0);
}

static int battle_resource_complete(unsigned phase)
{
    battle_texture_resolve();
    if (!battle_texture_fence || !battle_texture_wait || !battle_texture_delete) {
        battle_progress_event("bres_missing_api", phase, 0, 0, 0, 0);
        g_battle_resource_failed = 1;
        return 0;
    }
    battle_progress_event("bres_fence_before", phase, 0, 0, 0, 0);
    GLsync sync = battle_texture_fence(0x9117, 0);
    uint64_t id = (uint64_t)(uintptr_t)sync;
    battle_progress_event("bres_fence_after", phase, (int)id, (int)(id >> 32), 0, 0);
    if (!sync) {
        g_battle_resource_failed = 1;
        return 0;
    }
    battle_progress_event("bres_wait_before", phase, (int)id, (int)(id >> 32), 1, 1000000000);
    GLenum result = battle_texture_wait(sync, 1, 1000000000ULL);
    battle_progress_event("bres_wait_after", result, (int)id, (int)(id >> 32), phase, 0);
    if (result != 0x911a && result != 0x911c) {
        battle_aux_snapshot();
        battle_driver_errors_failure(sync, phase, result);
    }
    battle_progress_event("bres_delete_before", phase, (int)id, (int)(id >> 32), 0, 0);
    battle_texture_delete(sync);
    battle_progress_event("bres_delete_after", phase, (int)id, (int)(id >> 32), 0, 0);
    if (result == 0x911a || result == 0x911c) return 1;
    battle_progress_event("bres_failure", result, phase, 0, 0, 0);
    g_battle_resource_failed = 1;
    battle_load_trace_freeze();
    return 0;
}

static void battle_resource_describe(unsigned kind, GLuint texture, GLint level)
{
    if (kind != 2 && kind != 3 && kind != 5 && kind != 6) return;
    void (WINAPI *get)(GLenum, GLint *) = (void *)battle_frame_proc("glGetIntegerv");
    if (get) {
        GLint pbo = -1, alignment = 0, row = 0, height = 0;
        GLint x = 0, y = 0, z = 0, swap = 0, lsb = 0;
        GLint bw = 0, bh = 0, bd = 0, bs = 0;
        get(0x88ef, &pbo); get(0x0cf5, &alignment); get(0x0cf2, &row); get(0x806e, &height);
        get(0x0cf4, &x); get(0x0cf3, &y); get(0x806d, &z);
        get(0x0cf0, &swap); get(0x0cf1, &lsb);
        get(0x9127, &bw); get(0x9128, &bh); get(0x9129, &bd); get(0x912a, &bs);
        battle_progress_event("bres_unpack", 0, pbo, alignment, row, height);
        battle_progress_event("bres_skip", 0, x, y, z, 0);
        battle_progress_event("bres_order", 0, swap, lsb, 0, 0);
        battle_progress_event("bres_block", 0, bw, bh, bd, bs);
        if (pbo > 0) {
            void (WINAPI *size_get)(GLuint, GLenum, GLint64 *) =
                (void *)battle_frame_proc("glGetNamedBufferParameteri64v");
            void (WINAPI *state_get)(GLuint, GLenum, GLint *) =
                (void *)battle_frame_proc("glGetNamedBufferParameteriv");
            GLint64 size = -1; GLint mapped = -1;
            if (size_get) size_get((GLuint)pbo, 0x8764, &size);
            if (state_get) state_get((GLuint)pbo, 0x88bc, &mapped);
            battle_progress_event("bres_pbo_size", (GLenum)pbo,
                (int)size, (int)((uint64_t)size >> 32), mapped, 0);
        }
    } else {
        battle_progress_event("bres_desc_missing", 0, 0, 0, 0, 0);
    }
    GLboolean (WINAPI *valid)(GLuint) = (void *)battle_frame_proc("glIsTexture");
    void (WINAPI *get_level)(GLuint, GLint, GLenum, GLint *) =
        (void *)battle_frame_proc("glGetTextureLevelParameteriv");
    if (!valid || !get_level || !texture || level < 0 || !valid(texture)) return;
    GLint width = 0, height = 0, depth = 0, format = 0;
    get_level(texture, level, 0x1000, &width); get_level(texture, level, 0x1001, &height);
    get_level(texture, level, 0x8071, &depth); get_level(texture, level, 0x1003, &format);
    battle_progress_event("bres_tex_shape", (GLenum)format, texture, width, height, depth);
}

static unsigned battle_resource_before(const char *event, GLenum format,
                                      int a, int b, int c, int d)
{
    if (!g_battle_resource_boundary_on || !g_battle_load_trace_on ||
        !g_battle_observe_armed || g_battle_resource_failed) return 0;
    unsigned kind = battle_resource_kind(event);
    if (!kind) return 0;
    DWORD saved_error = GetLastError();
    AcquireSRWLockExclusive(&g_battle_resource_lock);
    if (!battle_current_context) battle_progress_event("bres_begin", 0, 0, 0, 0, 0);
    HGLRC context = battle_current_context ? battle_current_context() : NULL;
    if (!g_battle_resource_context && context) {
        g_battle_resource_context = context;
        g_battle_resource_thread = GetCurrentThreadId();
    }
    int active = context && context == g_battle_resource_context &&
                 GetCurrentThreadId() == g_battle_resource_thread;
    ReleaseSRWLockExclusive(&g_battle_resource_lock);
    if (active) {
        battle_progress_event("bres_operation", kind, a, b, c, d);
        battle_progress_event("bres_format", format, 0, 0, 0, 0);
        if (kind == 8 || kind == 9) battle_present_describe((HDC)(uintptr_t)
            ((uint64_t)(uint32_t)a | ((uint64_t)(uint32_t)b << 32)));
        if (battle_resource_complete(0)) {
            battle_resource_describe(kind, (GLuint)a, b);
            battle_progress_event("bres_call_before", kind, a, b, c, d);
        } else {
            active = 0;
        }
    }
    SetLastError(saved_error);
    return active ? kind : 0;
}

static void battle_resource_after(unsigned active)
{
    if (!active) return;
    DWORD saved_error = GetLastError();
    battle_progress_event("bres_call_after", active, 0, 0, 0, 0);
    battle_resource_complete(1);
    SetLastError(saved_error);
}
