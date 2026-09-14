/* Serialize only the first submission boundary to disambiguate async retirement. */
static int g_battle_frame_entry_on, g_battle_frame_failed, g_battle_frame_seen;
static uint64_t g_battle_frame_seen_number;
static HGLRC g_battle_frame_context;
static DWORD g_battle_frame_thread;
static SRWLOCK g_battle_frame_lock = SRWLOCK_INIT;

static PROC battle_frame_proc(const char *name)
{
    PROC p = real_wglGetProcAddress ? real_wglGetProcAddress(name) : NULL;
    if ((uintptr_t)p > 3 && p != (PROC)(intptr_t)-1) return p;
    HMODULE provider = GetModuleHandleA("opengl32real.dll");
    return provider ? GetProcAddress(provider, name) : NULL;
}

static int battle_frame_complete(unsigned phase)
{
    battle_texture_resolve();
    if (!battle_texture_fence || !battle_texture_wait || !battle_texture_delete) {
        battle_progress_event("bframe_missing_api", phase, 0, 0, 0, 0);
        g_battle_frame_failed = 1;
        return 0;
    }
    battle_progress_event("bframe_fence_before", phase, 0, 0, 0, 0);
    GLsync sync = battle_texture_fence(0x9117, 0);
    uint64_t id = (uint64_t)(uintptr_t)sync;
    battle_progress_event("bframe_fence_after", phase, (int)id, (int)(id >> 32), 0, 0);
    if (!sync) {
        g_battle_frame_failed = 1;
        return 0;
    }
    battle_progress_event("bframe_wait_before", phase, (int)id, (int)(id >> 32), 1, 1000000000);
    GLenum result = battle_texture_wait(sync, 1, 1000000000ULL);
    battle_progress_event("bframe_wait_after", result, (int)id, (int)(id >> 32), phase, 0);
    battle_progress_event("bframe_delete_before", phase, (int)id, (int)(id >> 32), 0, 0);
    battle_texture_delete(sync);
    battle_progress_event("bframe_delete_after", phase, (int)id, (int)(id >> 32), 0, 0);
    if (result == 0x911a || result == 0x911c) return 1;
    battle_progress_event("bframe_failure", result, phase, 0, 0, 0);
    g_battle_frame_failed = 1;
    battle_load_trace_freeze();
    return 0;
}

static void battle_frame_texture(GLuint texture, GLint level, unsigned role)
{
    void (WINAPI *get_level)(GLuint, GLint, GLenum, GLint *) =
        (void *)battle_frame_proc("glGetTextureLevelParameteriv");
    if (!texture || !get_level) return;
    GLint w = 0, h = 0, format = 0;
    get_level(texture, level, 0x1000, &w);
    get_level(texture, level, 0x1001, &h);
    get_level(texture, level, 0x1003, &format);
    battle_progress_event("bframe_tex_shape", role, texture, w, h, format);
}

static void battle_frame_attachment(GLuint framebuffer, GLenum attachment, unsigned role)
{
    void (WINAPI *get)(GLuint, GLenum, GLenum, GLint *) =
        (void *)battle_frame_proc("glGetNamedFramebufferAttachmentParameteriv");
    if (!framebuffer || !get) return;
    GLint type = 0, name = 0, level = 0, layered = 0;
    get(framebuffer, attachment, 0x8cd0, &type);
    if (type) get(framebuffer, attachment, 0x8cd1, &name);
    if (type == 0x1702) {
        get(framebuffer, attachment, 0x8cd2, &level);
        get(framebuffer, attachment, 0x8da7, &layered);
    }
    battle_progress_event("bframe_attachment", attachment, type, name, level, layered);
    if (type == 0x1702) battle_frame_texture((GLuint)name, level, role);
}

static void battle_frame_describe(void)
{
    void (WINAPI *get)(GLenum, GLint *) = (void *)battle_frame_proc("glGetIntegerv");
    void (WINAPI *get_i)(GLenum, GLuint, GLint *) = (void *)battle_frame_proc("glGetIntegeri_v");
    GLint (WINAPI *location)(GLuint, const char *) = (void *)battle_frame_proc("glGetUniformLocation");
    void (WINAPI *uniform)(GLuint, GLint, GLint *) = (void *)battle_frame_proc("glGetUniformiv");
    void (WINAPI *texture_parameter)(GLuint, GLenum, GLint *) = (void *)battle_frame_proc("glGetTextureParameteriv");
    void (WINAPI *sampler_parameter)(GLuint, GLenum, GLint *) = (void *)battle_frame_proc("glGetSamplerParameteriv");
    if (!get || !get_i || !location || !uniform || !texture_parameter || !sampler_parameter) {
        battle_progress_event("bframe_desc_missing", 0, 0, 0, 0, 0);
        return;
    }
    GLint program = 0, draw = 0, read = 0, vao = 0, viewport[4] = {0};
    get(0x8b8d, &program); get(0x8ca6, &draw); get(0x8caa, &read); get(0x85b5, &vao);
    get(0x0ba2, viewport);
    battle_progress_event("bframe_state", 0, program, draw, read, vao);
    battle_progress_event("bframe_viewport", 0, viewport[0], viewport[1], viewport[2], viewport[3]);
    battle_frame_attachment((GLuint)draw, 0x8d00, 1);
    battle_frame_attachment((GLuint)draw, 0x8ce0, 2);
    if (program <= 0) return;
    GLint loc = location((GLuint)program, "g_depth_sampler");
    if (loc < 0) return;
    GLint unit = -1, units = 0, texture = 0, sampler = 0;
    uniform((GLuint)program, loc, &unit); get(0x8b4d, &units);
    if (unit < 0 || unit >= units) return;
    get_i(0x8069, (GLuint)unit, &texture);
    get_i(0x8919, (GLuint)unit, &sampler);
    battle_progress_event("bframe_depth_input", 0, unit, texture, sampler, loc);
    if (texture <= 0) return;
    GLint base = 0, max = 0, min = 0, mag = 0, compare = 0, func = 0;
    texture_parameter((GLuint)texture, 0x813c, &base);
    texture_parameter((GLuint)texture, 0x813d, &max);
    if (sampler) {
        sampler_parameter((GLuint)sampler, 0x2801, &min);
        sampler_parameter((GLuint)sampler, 0x2800, &mag);
        sampler_parameter((GLuint)sampler, 0x884c, &compare);
        sampler_parameter((GLuint)sampler, 0x884d, &func);
    } else {
        texture_parameter((GLuint)texture, 0x2801, &min);
        texture_parameter((GLuint)texture, 0x2800, &mag);
        texture_parameter((GLuint)texture, 0x884c, &compare);
        texture_parameter((GLuint)texture, 0x884d, &func);
    }
    battle_progress_event("bframe_depth_levels", 0, texture, base, max, sampler);
    battle_progress_event("bframe_depth_filter", 0, min, mag, compare, func);
    battle_frame_texture((GLuint)texture, base, 0);
}

static int battle_frame_entry_before(const char *event)
{
    if (!g_battle_frame_entry_on || !g_battle_load_trace_on || !g_battle_observe_armed ||
        g_battle_frame_failed) return 0;
    unsigned kind = battle_pass_kind(event);
    if (kind != 1 && kind != 2) return 0;
    DWORD saved_error = GetLastError();
    int active = 0;
    AcquireSRWLockExclusive(&g_battle_frame_lock);
    if (!battle_current_context) battle_progress_event("bframe_begin", 0, 0, 0, 0, 0);
    HGLRC context = battle_current_context ? battle_current_context() : NULL;
    if (!g_battle_frame_context && context) {
        g_battle_frame_context = context;
        g_battle_frame_thread = GetCurrentThreadId();
    }
    if (context && context == g_battle_frame_context &&
        GetCurrentThreadId() == g_battle_frame_thread &&
        (!g_battle_frame_seen || g_battle_frame_seen_number != g_frame_count)) {
        g_battle_frame_seen = 1;
        g_battle_frame_seen_number = g_frame_count;
        active = 1;
    }
    ReleaseSRWLockExclusive(&g_battle_frame_lock);
    if (active && battle_frame_complete(0)) {
        battle_frame_describe();
    } else {
        active = 0;
    }
    SetLastError(saved_error);
    return active;
}

static void battle_frame_entry_after(int active)
{
    if (!active) return;
    DWORD saved_error = GetLastError();
    battle_frame_complete(1);
    SetLastError(saved_error);
}
