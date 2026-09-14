/* Opt-in CPU trace. No GL calls. Fixed-size mapped ring retains the recent
   tail after forced process termination; a CPU return is not GPU completion. */
#ifndef BATTLE_LOAD_TRACE_DIR
#define BATTLE_LOAD_TRACE_DIR "C:\\fgo\\_tools\\glshim\\"
#endif
#ifndef BATTLE_LOAD_TRACE_CAPACITY
#define BATTLE_LOAD_TRACE_CAPACITY 262144u
#endif

typedef struct {
    char magic[8];
    uint32_t version, capacity, record_size, pid;
    volatile LONG64 committed;
    uint64_t start_tick;
    uint32_t header_size, flags;
    unsigned char reserved[80];
} battle_trace_header;
typedef struct {
    volatile LONG64 sequence;
    uint64_t tick, frame;
    uint32_t thread, program, shader, type;
    int32_t a, b, c, d;
    uint32_t framebuffer, vao;
    char event[40];
    unsigned char reserved[24];
} battle_trace_record;
_Static_assert(sizeof(battle_trace_header) == 128, "trace header ABI");
_Static_assert(sizeof(battle_trace_record) == 128, "trace record ABI");

static int g_battle_load_trace_on;
static volatile LONG g_battle_trace_frozen;
static SRWLOCK g_battle_trace_lock = SRWLOCK_INIT;
static battle_trace_header *g_battle_trace_header;
static HANDLE g_battle_trace_file = INVALID_HANDLE_VALUE;
static HANDLE g_battle_trace_mapping;
static HANDLE g_battle_trace_sources = INVALID_HANDLE_VALUE;
static uint64_t g_battle_trace_source_bytes;
static int g_battle_trace_open_attempted;
static int g_battle_trace_sources_failed;

/* Called under the private trace lock. The OS owns cleanup on process exit;
   no GL work or worker thread is introduced by this recorder. */
static void battle_load_trace_open_locked(void)
{
    char path[MAX_PATH];
    const DWORD size = (DWORD)(sizeof(battle_trace_header) +
        BATTLE_LOAD_TRACE_CAPACITY * sizeof(battle_trace_record));
    if (g_battle_trace_open_attempted) return;
    g_battle_trace_open_attempted = 1;
    snprintf(path, sizeof path, BATTLE_LOAD_TRACE_DIR "battle_load_v2_%lu.bin",
             (unsigned long)GetCurrentProcessId());
    g_battle_trace_file = CreateFileA(path, GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_DELETE, NULL, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, NULL);
    if (g_battle_trace_file == INVALID_HANDLE_VALUE) return;
    g_battle_trace_mapping = CreateFileMappingA(g_battle_trace_file, NULL,
        PAGE_READWRITE, 0, size, NULL);
    if (!g_battle_trace_mapping) {
        CloseHandle(g_battle_trace_file);
        g_battle_trace_file = INVALID_HANDLE_VALUE;
        return;
    }
    g_battle_trace_header = (battle_trace_header *)MapViewOfFile(
        g_battle_trace_mapping, FILE_MAP_WRITE, 0, 0, size);
    if (!g_battle_trace_header) {
        CloseHandle(g_battle_trace_mapping);
        CloseHandle(g_battle_trace_file);
        g_battle_trace_mapping = NULL;
        g_battle_trace_file = INVALID_HANDLE_VALUE;
        return;
    }
    memset(g_battle_trace_header, 0, sizeof *g_battle_trace_header);
    memcpy(g_battle_trace_header->magic, "BTLTv2", 6);
    g_battle_trace_header->version = 2;
    g_battle_trace_header->capacity = BATTLE_LOAD_TRACE_CAPACITY;
    g_battle_trace_header->record_size = sizeof(battle_trace_record);
    g_battle_trace_header->pid = GetCurrentProcessId();
    g_battle_trace_header->start_tick = GetTickCount64();
    g_battle_trace_header->header_size = sizeof(battle_trace_header);
}

static void battle_load_trace_event(const char *event, uint64_t frame,
    unsigned int program, unsigned int shader, unsigned int type,
    int a, int b, int c, int d)
{
    if (!g_battle_load_trace_on || g_battle_trace_frozen) return;
    DWORD saved_error = GetLastError();
    AcquireSRWLockExclusive(&g_battle_trace_lock);
    battle_load_trace_open_locked();
    if (g_battle_trace_header && !g_battle_trace_frozen) {
        LONG64 seq = g_battle_trace_header->committed + 1;
        battle_trace_record *records = (battle_trace_record *)(g_battle_trace_header + 1);
        battle_trace_record *record = &records[(seq - 1) % BATTLE_LOAD_TRACE_CAPACITY];
        InterlockedExchange64(&record->sequence, 0);
        record->tick = GetTickCount64();
        record->frame = frame;
        record->thread = GetCurrentThreadId();
        record->program = program;
        record->shader = shader;
        record->type = type;
        record->a = a; record->b = b; record->c = c; record->d = d;
        record->framebuffer = g_bound_draw_framebuffer;
        record->vao = g_current_vao;
        memset(record->event, 0, sizeof record->event);
        if (event) strncpy(record->event, event, sizeof record->event - 1);
        /* Publish only complete records. Readers discard torn/overwritten slots. */
        InterlockedExchange64(&record->sequence, seq);
        InterlockedExchange64(&g_battle_trace_header->committed, seq);
    }
    ReleaseSRWLockExclusive(&g_battle_trace_lock);
    SetLastError(saved_error);
}

/* Snapshot CPU-cached submitted GLSL and program associations at link time.
   Separate file keeps these cold records when the event ring wraps. */
static void battle_load_trace_source(unsigned int program, unsigned int shader,
    unsigned int type, const char *source, int length, unsigned int replaced)
{
    if (!g_battle_load_trace_on || g_battle_trace_frozen || !source || length <= 0) return;
    DWORD saved_error = GetLastError();
    AcquireSRWLockExclusive(&g_battle_trace_lock);
    battle_load_trace_open_locked();
    if (g_battle_trace_frozen || g_battle_trace_sources_failed) {
        /* Never append another chunk after a short/failed write. */
    } else if (g_battle_trace_header &&
        g_battle_trace_source_bytes + (uint64_t)length + 24 <= 128u * 1024u * 1024u) {
        if (g_battle_trace_sources == INVALID_HANDLE_VALUE) {
            char path[MAX_PATH];
            snprintf(path, sizeof path, BATTLE_LOAD_TRACE_DIR "battle_load_v2_%lu.shaders.bin",
                     (unsigned long)GetCurrentProcessId());
            g_battle_trace_sources = CreateFileA(path, GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_DELETE, NULL, CREATE_ALWAYS,
                FILE_ATTRIBUTE_NORMAL, NULL);
        }
        if (g_battle_trace_sources != INVALID_HANDLE_VALUE) {
            uint32_t meta[6] = {0x32534842u, program, shader, type,
                               (uint32_t)length, replaced};
            DWORD written = 0;
            int ok = WriteFile(g_battle_trace_sources, meta, sizeof meta, &written, NULL) &&
                     written == sizeof meta;
            if (ok) ok = WriteFile(g_battle_trace_sources, source, (DWORD)length, &written, NULL) &&
                         written == (DWORD)length;
            if (ok) g_battle_trace_source_bytes += sizeof meta + (uint64_t)length;
            else {
                g_battle_trace_header->flags |= 2u;
                g_battle_trace_sources_failed = 1;
            }
        } else {
            g_battle_trace_header->flags |= 2u;
            g_battle_trace_sources_failed = 1;
        }
    } else if (g_battle_trace_header) g_battle_trace_header->flags |= 1u;
    ReleaseSRWLockExclusive(&g_battle_trace_lock);
    SetLastError(saved_error);
}

static void battle_load_trace_freeze(void)
{
    DWORD saved_error = GetLastError();
    AcquireSRWLockExclusive(&g_battle_trace_lock);
    if (g_battle_trace_header) g_battle_trace_header->flags |= 4u;
    InterlockedExchange(&g_battle_trace_frozen, 1);
    ReleaseSRWLockExclusive(&g_battle_trace_lock);
    SetLastError(saved_error);
}

/* Scalar metadata only; do not dereference application draw payloads here. */
#define BATTLE_TRACE_DRAW(event, mode, a, b, c, d) do { \
    if (g_battle_load_trace_on) \
        battle_load_trace_event(event, g_frame_count, g_current_program, 0, \
                                mode, a, b, c, d); \
} while (0)
