#ifndef SUBMISSION_GPU_PROFILE_H
#define SUBMISSION_GPU_PROFILE_H

/* Diagnostic: one frame per 120 presents. Timestamp spans include intervening
   copies, barriers and submission gaps; they are not pure shader execution time.
   Results are read at least eight presents later, and only when available. */
#ifdef FGO_SUBMISSION_GPU_PROFILE
#define SUBMISSION_GPU_SEGMENTS 256u
#define SUBMISSION_GPU_RECORDS 4096u
typedef struct {
    uint64_t sequence, frame, program, framebuffer, kind, draws;
    uint64_t cpu_begin, cpu_end, gpu_begin, gpu_end;
    uint64_t swap_begin, swap_end, truncated, sequence_end;
} submission_gpu_record;
static submission_gpu_record g_submission_gpu_records[SUBMISSION_GPU_RECORDS]
    __attribute__((used));
static volatile LONG64 g_submission_gpu_sequence;
static volatile uint64_t g_submission_gpu_sample_frames, g_submission_gpu_unavailable;
static volatile uint64_t g_submission_gpu_disabled, g_submission_gpu_context_resets;
static submission_gpu_record submission_gpu_pending[SUBMISSION_GPU_SEGMENTS];
static unsigned submission_gpu_queries[SUBMISSION_GPU_SEGMENTS + 1];
static unsigned submission_gpu_count, submission_gpu_depth;
static uint64_t submission_gpu_presents, submission_gpu_closed_at;
static DWORD submission_gpu_thread;
static HGLRC submission_gpu_context;
static int submission_gpu_active, submission_gpu_waiting;
static void (WINAPI *submission_gpu_gen)(int, unsigned *);
static void (WINAPI *submission_gpu_stamp)(unsigned, unsigned);
static void (WINAPI *submission_gpu_available)(unsigned, unsigned, int *);
static void (WINAPI *submission_gpu_result)(unsigned, unsigned, uint64_t *);

static uint64_t submission_gpu_clock(void)
{
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return now.QuadPart;
}

static void submission_gpu_context_change(HGLRC context)
{
    if (submission_gpu_context == context) return;
    submission_gpu_context = context;
    submission_gpu_active = submission_gpu_waiting = 0;
    submission_gpu_count = submission_gpu_depth = 0;
    submission_gpu_thread = 0;
    /* Query names belong to the old context. Never read/reuse them here. */
    memset(submission_gpu_queries, 0, sizeof submission_gpu_queries);
    g_submission_gpu_context_resets++;
}

static int submission_gpu_init(void)
{
    if (g_submission_gpu_disabled) return 0;
    if (submission_gpu_queries[0]) return 1;
    *(PROC *)&submission_gpu_gen = trace_resolve("glGenQueries");
    *(PROC *)&submission_gpu_stamp = trace_resolve("glQueryCounter");
    *(PROC *)&submission_gpu_available = trace_resolve("glGetQueryObjectiv");
    *(PROC *)&submission_gpu_result = trace_resolve("glGetQueryObjectui64v");
    if (!submission_gpu_gen || !submission_gpu_stamp ||
        !submission_gpu_available || !submission_gpu_result) {
        g_submission_gpu_disabled = 1;
        return 0;
    }
    submission_gpu_gen(SUBMISSION_GPU_SEGMENTS + 1, submission_gpu_queries);
    if (!submission_gpu_queries[0]) g_submission_gpu_disabled = 1;
    return !g_submission_gpu_disabled;
}

static void submission_gpu_mark(unsigned program, unsigned framebuffer, unsigned kind)
{
    if (!submission_gpu_active || submission_gpu_thread != GetCurrentThreadId()) return;
    unsigned n = submission_gpu_count;
    if (n) {
        submission_gpu_record *last = &submission_gpu_pending[n - 1];
        if (last->program == program && last->framebuffer == framebuffer && last->kind == kind) {
            last->draws++;
            return;
        }
        if (n == SUBMISSION_GPU_SEGMENTS) {
            last->truncated++;
            return;
        }
    }
    uint64_t now = submission_gpu_clock();
    submission_gpu_stamp(submission_gpu_queries[n], 0x8E28 /* GL_TIMESTAMP */);
    if (n) submission_gpu_pending[n - 1].cpu_end = now;
    submission_gpu_record *out = &submission_gpu_pending[n];
    memset(out, 0, sizeof *out);
    out->program = program; out->framebuffer = framebuffer; out->kind = kind;
    out->draws = 1; out->cpu_begin = now;
    submission_gpu_count++;
}

static void submission_gpu_drain(void)
{
    if (!submission_gpu_waiting || submission_gpu_presents - submission_gpu_closed_at < 8) return;
    /* Checking availability must precede EVERY result read. No glFinish/Flush. */
    for (unsigned i = 0; i <= submission_gpu_count; i++) {
        int ready = 0;
        submission_gpu_available(submission_gpu_queries[i], 0x8867, &ready);
        if (!ready) { g_submission_gpu_unavailable++; return; }
    }
    uint64_t begin;
    submission_gpu_result(submission_gpu_queries[0], 0x8866, &begin);
    for (unsigned i = 0; i < submission_gpu_count; i++) {
        uint64_t end;
        submission_gpu_result(submission_gpu_queries[i + 1], 0x8866, &end);
        submission_gpu_record *r = &submission_gpu_pending[i];
        r->gpu_begin = begin; r->gpu_end = end; begin = end;
        LONG64 seq = g_submission_gpu_sequence + 1;
        submission_gpu_record *out = &g_submission_gpu_records[(seq - 1) % SUBMISSION_GPU_RECORDS];
        InterlockedExchange64((volatile LONG64 *)&out->sequence, 0);
        InterlockedExchange64((volatile LONG64 *)&out->sequence_end, 0);
        memcpy(&out->frame, &r->frame, sizeof(*out) - 16);
        InterlockedExchange64((volatile LONG64 *)&out->sequence_end, seq);
        InterlockedExchange64((volatile LONG64 *)&out->sequence, seq);
        InterlockedExchange64(&g_submission_gpu_sequence, seq);
    }
    submission_gpu_waiting = 0;
}

static void submission_gpu_swap_begin(void)
{
    if (!submission_gpu_thread) submission_gpu_thread = GetCurrentThreadId();
    if (submission_gpu_thread != GetCurrentThreadId() || submission_gpu_depth++) return;
    DWORD saved_error = GetLastError();
    if (submission_gpu_active) {
        submission_gpu_active = 0;
        if (submission_gpu_count) {
            uint64_t now = submission_gpu_clock();
            submission_gpu_pending[submission_gpu_count - 1].cpu_end = now;
            submission_gpu_stamp(submission_gpu_queries[submission_gpu_count], 0x8E28);
            for (unsigned i = 0; i < submission_gpu_count; i++)
                submission_gpu_pending[i].swap_begin = now;
            submission_gpu_waiting = 1;
            submission_gpu_closed_at = submission_gpu_presents;
        }
    }
    SetLastError(saved_error);
}

static void submission_gpu_swap_end(uint64_t frame)
{
    if (submission_gpu_thread != GetCurrentThreadId() || !submission_gpu_depth || --submission_gpu_depth) return;
    DWORD saved_error = GetLastError();
    if (submission_gpu_waiting && submission_gpu_closed_at == submission_gpu_presents) {
        uint64_t now = submission_gpu_clock();
        for (unsigned i = 0; i < submission_gpu_count; i++) {
            submission_gpu_pending[i].frame = frame;
            submission_gpu_pending[i].swap_end = now;
        }
    }
    submission_gpu_presents++;
    submission_gpu_drain();
    if (submission_gpu_presents % 120 == 1 && !submission_gpu_waiting && submission_gpu_init()) {
        submission_gpu_count = 0;
        submission_gpu_active = 1;
        g_submission_gpu_sample_frames++;
    }
    SetLastError(saved_error);
}
#else
#define submission_gpu_context_change(context) ((void)0)
#define submission_gpu_mark(program, framebuffer, kind) ((void)0)
#define submission_gpu_swap_begin() ((void)0)
#define submission_gpu_swap_end(frame) ((void)0)
#endif
#endif
