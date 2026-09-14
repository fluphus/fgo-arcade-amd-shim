/* Bounded driver messages survive the independently frozen draw trace. */
#define BATTLE_DRIVER_ERROR_CAPACITY 128u
typedef struct {
    volatile LONG64 sequence;
    uint64_t tick, context, fine_sequence, progress_sequence, frame;
    uint32_t thread, source, type, id, severity, length;
    char text[440];
} battle_driver_error_record;
_Static_assert(sizeof(battle_driver_error_record) == 512, "driver error ABI");
typedef void (WINAPI *battle_driver_callback_t)(GLenum, GLenum, GLuint, GLenum,
                                               GLsizei, const char *, const void *);
static battle_trace_header *g_battle_driver_errors;
static int g_battle_driver_errors_on;
static volatile LONG64 g_battle_driver_error_count;
static volatile LONG g_battle_driver_failure_recorded;
static HGLRC g_battle_driver_error_context;
static int g_battle_driver_error_attempted;

static void WINAPI battle_driver_error_callback(GLenum source, GLenum type, GLuint id,
    GLenum severity, GLsizei length, const char *message, const void *user)
{
    (void)user;
    if (!g_battle_driver_errors || (g_battle_driver_errors->flags & 4) || !message || length < 0 ||
        (type != 0x824c && type != 0x824e && severity != 0x9146)) return;
    DWORD saved_error = GetLastError();
    LONG64 sequence = InterlockedIncrement64(&g_battle_driver_error_count);
    {
        battle_driver_error_record *record =
            (battle_driver_error_record *)(g_battle_driver_errors + 1) +
            (sequence - 1) % BATTLE_DRIVER_ERROR_CAPACITY;
        InterlockedExchange64(&record->sequence, 0);
        record->tick = GetTickCount64();
        record->context = (uint64_t)(uintptr_t)g_battle_driver_error_context;
        record->fine_sequence = g_battle_trace_header ? g_battle_trace_header->committed : 0;
        record->progress_sequence = g_battle_progress ? g_battle_progress->committed : 0;
        record->frame = g_frame_count;
        record->thread = GetCurrentThreadId();
        record->source = source; record->type = type; record->id = id;
        record->severity = severity; record->length = (uint32_t)length;
        size_t copied = (size_t)length;
        if (copied >= sizeof record->text) copied = sizeof record->text - 1;
        memcpy(record->text, message, copied);
        record->text[copied] = 0;
        InterlockedExchange64(&record->sequence, sequence);
        LONG64 previous = g_battle_driver_errors->committed;
        while (previous < sequence) {
            LONG64 observed = InterlockedCompareExchange64(
                &g_battle_driver_errors->committed, sequence, previous);
            if (observed == previous) break;
            previous = observed;
        }
    }
    if (sequence > BATTLE_DRIVER_ERROR_CAPACITY) {
        InterlockedOr((volatile LONG *)&g_battle_driver_errors->flags, 2);
    }
    SetLastError(saved_error);
}

static void battle_driver_errors_arm(void)
{
    if (!g_battle_driver_errors_on || !g_battle_resource_boundary_on || !g_battle_observe_armed ||
        g_battle_driver_error_attempted) return;
    g_battle_driver_error_attempted = 1;
    DWORD saved_error = GetLastError();
    void (WINAPI *get_pointer)(GLenum, void **) = (void *)battle_frame_proc("glGetPointerv");
    void (WINAPI *callback)(battle_driver_callback_t, const void *) =
        (void *)battle_frame_proc("glDebugMessageCallback");
    void (WINAPI *enable)(GLenum) = (void *)battle_frame_proc("glEnable");
    void (WINAPI *get)(GLenum, GLint *) = (void *)battle_frame_proc("glGetIntegerv");
    void *existing = NULL;
    unsigned skipped = 0;
    if (!get_pointer || !callback || !enable || !get) skipped = 1;
    if (!skipped) {
        get_pointer(0x8244, &existing);
        if (existing) skipped = 2;
    }
    if (!skipped) {
        g_battle_driver_error_context = battle_current_context ? battle_current_context() : NULL;
        if (!g_battle_driver_error_context) skipped = 3;
    }
    if (!skipped) {
        char path[MAX_PATH];
        snprintf(path, sizeof path, BATTLE_LOAD_TRACE_DIR "battle_gl_errors_v1_%lu.bin",
                 (unsigned long)GetCurrentProcessId());
        DWORD bytes = sizeof(battle_trace_header) +
            BATTLE_DRIVER_ERROR_CAPACITY * sizeof(battle_driver_error_record);
        HANDLE file = CreateFileA(path, GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        HANDLE mapping = file != INVALID_HANDLE_VALUE ?
            CreateFileMappingA(file, NULL, PAGE_READWRITE, 0, bytes, NULL) : NULL;
        if (mapping) {
            g_battle_driver_errors = MapViewOfFile(mapping, FILE_MAP_WRITE, 0, 0, bytes);
            CloseHandle(mapping);
        }
        if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
        if (!g_battle_driver_errors) skipped = 4;
    }
    if (skipped) {
        battle_progress_event("bdebug_skipped", skipped, 0, 0, 0, 0);
    } else {
        memcpy(g_battle_driver_errors->magic, "BGLERR1", 7);
        g_battle_driver_errors->version = 1;
        g_battle_driver_errors->capacity = BATTLE_DRIVER_ERROR_CAPACITY;
        g_battle_driver_errors->record_size = sizeof(battle_driver_error_record);
        g_battle_driver_errors->pid = GetCurrentProcessId();
        g_battle_driver_errors->start_tick = GetTickCount64();
        g_battle_driver_errors->header_size = sizeof(battle_trace_header);
        GLint enabled = 0, synchronous = 0, flags = 0;
        get(0x92e0, &enabled); get(0x8242, &synchronous); get(0x821e, &flags);
        callback(battle_driver_error_callback, NULL);
        enable(0x92e0); enable(0x8242);
        get_pointer(0x8244, &existing);
        if (existing == (void *)battle_driver_error_callback)
            InterlockedOr((volatile LONG *)&g_battle_driver_errors->flags, 1);
        battle_progress_event("bdebug_install", g_battle_driver_errors->flags,
                              enabled, synchronous, flags, 0);
    }
    SetLastError(saved_error);
}

static void battle_driver_errors_failure(GLsync sync, unsigned phase, GLenum result)
{
    if (!g_battle_driver_errors_on || !g_battle_resource_boundary_on || !g_battle_observe_armed ||
        InterlockedCompareExchange(&g_battle_driver_failure_recorded, 1, 0)) return;
    DWORD saved_error = GetLastError();
    uint64_t id = (uint64_t)(uintptr_t)sync;
    battle_progress_event("bres_diagnose_before", result, phase, (int)id, (int)(id >> 32), 0);
    GLenum (WINAPI *error)(void) = (void *)battle_frame_proc("glGetError");
    GLboolean (WINAPI *valid)(GLsync) = (void *)battle_frame_proc("glIsSync");
    GLenum (WINAPI *reset)(void) = (void *)battle_frame_proc("glGetGraphicsResetStatus");
    if (!reset) reset = (void *)battle_frame_proc("glGetGraphicsResetStatusARB");
    /* Reading errors is restricted to failure; prior pending errors can be included. */
    for (unsigned i = 0; error && i < 8; i++) {
        GLenum code = error();
        battle_progress_event("bres_gl_error", code, phase, i, 0, 0);
        if (!code) break;
    }
    GLint is_sync = valid ? valid(sync) : -1;
    GLenum status = reset ? reset() : 0xffffffffu;
    battle_progress_event("bres_diagnose_after", status, phase, is_sync, error != NULL, 0);
    if (g_battle_driver_errors)
        InterlockedOr((volatile LONG *)&g_battle_driver_errors->flags, 4);
    SetLastError(saved_error);
}

static const int *battle_driver_debug_attributes(const int *input, int output[131])
{
    if (!g_battle_driver_errors_on || !g_battle_resource_boundary_on) return input;
    unsigned i = 0;
    int have_flags = 0;
    for (; i < 128 && input && input[i]; i += 2) {
        output[i] = input[i]; output[i + 1] = input[i + 1];
        if (input[i] == 0x2094) { output[i + 1] |= 1; have_flags = 1; }
    }
    if (i == 128) return input;
    if (!have_flags) { output[i++] = 0x2094; output[i++] = 1; }
    output[i] = 0;
    return output;
}
