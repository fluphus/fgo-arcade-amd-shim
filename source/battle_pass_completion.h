/* Owned, asynchronous checkpoints. A pending fence is not a failed draw. */
#define BATTLE_PASS_CAPACITY 1024u
#define BATTLE_PASS_STRIDE 32u
static SRWLOCK g_battle_pass_lock = SRWLOCK_INIT;
static struct {
    GLsync sync;
    LONG64 creation;
} g_battle_pass_queue[BATTLE_PASS_CAPACITY];
static unsigned g_battle_pass_head, g_battle_pass_count, g_battle_pass_since;
static int g_battle_pass_failed, g_battle_pass_full_reported;
static HGLRC g_battle_pass_context;
static DWORD g_battle_pass_thread;
static GLuint g_battle_pass_program, g_battle_pass_fbo;
static uint64_t g_battle_pass_frame;
static const char *g_battle_pass_event;

static unsigned battle_pass_kind(const char *event)
{
    if (!strcmp(event, "compute")) return 2;
    if (!strncmp(event, "tex_", 4)) return 3;
    if ((event[0] == 'd' && (event[1] == 'a' ||
         (event[1] == 'e' && (!event[2] || event[2] == '_')))) ||
        !strcmp(event, "dre") || !strncmp(event, "mda", 3) ||
        !strncmp(event, "mde", 3) || !strncmp(event, "nv_", 3)) return 1;
    return 0;
}

static int battle_pass_owns_context(void)
{
    return battle_current_context && g_battle_pass_context &&
        GetCurrentThreadId() == g_battle_pass_thread &&
        battle_current_context() == g_battle_pass_context;
}

static void battle_pass_poll_locked(unsigned limit)
{
    if (g_battle_pass_failed || !battle_pass_owns_context() ||
        !battle_texture_wait || !battle_texture_delete) return;
    while (g_battle_pass_count && limit--) {
        unsigned slot = g_battle_pass_head;
        uint64_t id = (uint64_t)(uintptr_t)g_battle_pass_queue[slot].sync;
        uint64_t creation = (uint64_t)g_battle_pass_queue[slot].creation;
        battle_progress_event("bpass_poll_before", 0, (int)id, (int)(id >> 32),
                              (int)creation, (int)(creation >> 32));
        GLenum result = battle_texture_wait(g_battle_pass_queue[slot].sync, 0, 0);
        battle_progress_event("bpass_poll_after", result, (int)id, (int)(id >> 32),
                              (int)creation, (int)(creation >> 32));
        if (result == 0x911b) break;
        if (result != 0x911a && result != 0x911c) {
            g_battle_pass_failed = 1;
            break;
        }
        battle_progress_event("bpass_delete_before", 0, (int)id, (int)(id >> 32),
                              (int)creation, (int)(creation >> 32));
        battle_texture_delete(g_battle_pass_queue[slot].sync);
        battle_progress_event("bpass_delete_after", 0, (int)id, (int)(id >> 32),
                              (int)creation, (int)(creation >> 32));
        g_battle_pass_queue[slot].sync = NULL;
        g_battle_pass_head = (slot + 1) % BATTLE_PASS_CAPACITY;
        g_battle_pass_count--;
        g_battle_pass_full_reported = 0;
    }
}

static void battle_pass_poll(void)
{
    if (!g_battle_texture_boundary_on || !g_battle_load_trace_on ||
        !g_battle_observe_armed) return;
    DWORD saved_error = GetLastError();
    AcquireSRWLockExclusive(&g_battle_pass_lock);
    battle_pass_poll_locked(BATTLE_PASS_CAPACITY);
    ReleaseSRWLockExclusive(&g_battle_pass_lock);
    SetLastError(saved_error);
}

static void battle_pass_after_call(const char *event)
{
    if (!g_battle_texture_boundary_on || !g_battle_load_trace_on ||
        !g_battle_observe_armed) return;
    unsigned kind = battle_pass_kind(event);
    if (!kind) return;
    DWORD saved_error = GetLastError();
    AcquireSRWLockExclusive(&g_battle_pass_lock);
    if (g_battle_pass_failed) goto done;
    if (!g_battle_pass_context) {
        battle_progress_event("bpass_begin", 0, BATTLE_PASS_STRIDE, BATTLE_PASS_CAPACITY, 0, 0);
        HGLRC context = battle_current_context ? battle_current_context() : NULL;
        if (!context) goto done;
        g_battle_pass_context = context;
        g_battle_pass_thread = GetCurrentThreadId();
        battle_texture_resolve();
    }
    if (!battle_pass_owns_context() || !battle_texture_fence ||
        !battle_texture_wait || !battle_texture_delete) goto done;
    unsigned reason = 0;
    if (!g_battle_pass_event || strcmp(g_battle_pass_event, event) ||
        g_battle_pass_program != g_current_program ||
        g_battle_pass_fbo != g_bound_draw_framebuffer ||
        g_battle_pass_frame != g_frame_count) reason |= 1;
    if (++g_battle_pass_since >= BATTLE_PASS_STRIDE) reason |= 2;
    if (kind == 2) reason |= 4;
    if (!reason) goto done;
    g_battle_pass_event = event;
    g_battle_pass_program = g_current_program;
    g_battle_pass_fbo = g_bound_draw_framebuffer;
    g_battle_pass_frame = g_frame_count;
    g_battle_pass_since = 0;
    battle_pass_poll_locked(64);
    if (g_battle_pass_failed) goto done;
    if (g_battle_pass_count == BATTLE_PASS_CAPACITY) {
        if (!g_battle_pass_full_reported) {
            battle_progress_event("bpass_full", 0, g_battle_pass_count, 0, 0, 0);
            g_battle_pass_full_reported = 1;
        }
        goto done;
    }
    battle_progress_event("bpass_fence_before", reason, kind, 0, 0, 0);
    GLsync sync = battle_texture_fence(0x9117, 0);
    uint64_t id = (uint64_t)(uintptr_t)sync;
    LONG64 creation = battle_progress_event("bpass_fence_after", reason,
                                          (int)id, (int)(id >> 32), kind, 0);
    if (sync) {
        unsigned slot = (g_battle_pass_head + g_battle_pass_count) % BATTLE_PASS_CAPACITY;
        g_battle_pass_queue[slot].sync = sync;
        g_battle_pass_queue[slot].creation = creation;
        g_battle_pass_count++;
    }
done:
    ReleaseSRWLockExclusive(&g_battle_pass_lock);
    SetLastError(saved_error);
}

static void battle_texture_upload_args(GLuint texture, GLint level,
    GLint x, GLint y, GLint z, GLsizei depth, GLenum type, GLsizei size, const void *data)
{
    if (!g_battle_texture_boundary_on) return;
    uint64_t pointer = (uint64_t)(uintptr_t)data;
    BATTLE_TRACE_DRAW("tex_upload_offset", type, x, y, z, depth);
    BATTLE_TRACE_DRAW("tex_upload_pointer", texture, level, size,
                      (int)pointer, (int)(pointer >> 32));
}
