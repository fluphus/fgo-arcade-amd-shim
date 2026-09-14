/* Experimental presentation only: retain all rendered pixels, replace the swap. */
static void (WINAPI *battle_front_draw_buffer)(GLenum);
static void (WINAPI *battle_front_blit)(GLint, GLint, GLint, GLint,
    GLint, GLint, GLint, GLint, GLbitfield, GLenum);
static void (WINAPI *battle_front_flush)(void);
static HGLRC battle_front_owner;
static DWORD battle_front_thread;
static SRWLOCK battle_front_lock = SRWLOCK_INIT;

static int battle_front_prepare(HDC dc, GLint *width, GLint *height)
{
    if (!g_battle_present_front_blit_on || !g_battle_observe_armed) return 0;
    DWORD saved_error = GetLastError();
    void (WINAPI *get)(GLenum, GLint *) = (void *)battle_frame_proc("glGetIntegerv");
    HDC (WINAPI *get_dc)(void) = (void *)battle_frame_proc("wglGetCurrentDC");
    HGLRC (WINAPI *get_context)(void) = (void *)battle_frame_proc("wglGetCurrentContext");
    void (WINAPI *draw_buffer)(GLenum) = (void *)battle_frame_proc("glDrawBuffer");
    void (WINAPI *blit)(GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum) =
        (void *)battle_frame_proc("glBlitFramebuffer");
    void (WINAPI *flush)(void) = (void *)battle_frame_proc("glFlush");
    unsigned reason = 0;
    HGLRC context = NULL;
    if (!get || !get_dc || !get_context || !draw_buffer || !blit || !flush) reason = 1;
    if (!reason) {
        context = get_context();
        if (!context || get_dc() != dc) reason = 2;
    }
    GLint draw = -1, read = -1, db = -1, rb = -1, samples = -1, double_buffer = -1;
    GLint scissor = -1, srgb = -1;
    if (!reason) {
        get(0x8ca6, &draw); get(0x8caa, &read);
        get(0x0c01, &db); get(0x0c02, &rb);
        get(0x80a9, &samples); get(0x0c32, &double_buffer);
        get(0x0c11, &scissor); get(0x8db9, &srgb);
        if (draw || read || db != 0x405 || rb != 0x405 || samples || !double_buffer ||
            scissor || srgb) reason = 3;
    }
    RECT client = {0};
    if (!reason) {
        HWND window = WindowFromDC(dc);
        if (!window || !GetClientRect(window, &client) || client.right <= 0 ||
            client.bottom <= 0 || client.right > 16384 || client.bottom > 16384) reason = 4;
    }
    if (!reason) {
        DWORD thread = GetCurrentThreadId();
        AcquireSRWLockExclusive(&battle_front_lock);
        if (!battle_front_owner) {
            battle_front_owner = context; battle_front_thread = thread;
            battle_front_draw_buffer = draw_buffer; battle_front_blit = blit; battle_front_flush = flush;
        }
        if (battle_front_owner != context || battle_front_thread != thread) reason = 5;
        ReleaseSRWLockExclusive(&battle_front_lock);
    }
    if (reason) {
        battle_progress_event("bfront_fallback", reason, draw, read, db, rb);
    } else {
        *width = client.right; *height = client.bottom;
    }
    SetLastError(saved_error);
    return reason == 0;
}

static BOOL battle_front_present(GLint width, GLint height)
{
    DWORD saved_error = GetLastError();
    battle_front_draw_buffer(0x404);
    battle_front_blit(0, 0, width, height, 0, 0, width, height, 0x4000, 0x2600);
    battle_front_draw_buffer(0x405);
    battle_front_flush();
    SetLastError(saved_error);
    return TRUE;
}
