#ifndef FGO_PRESENT_TIMELINE_H
#define FGO_PRESENT_TIMELINE_H

enum { PRESENT_TIME_FINISH, PRESENT_TIME_FLUSH, PRESENT_TIME_CLIENT_WAIT,
       PRESENT_TIME_SERVER_WAIT };

#ifdef FGO_PRESENT_TIMELINE
/* Diagnostic only: one presenting thread, CPU timestamps, no GL operations. */
#define PRESENT_TIMELINE_CAPACITY 4096u
typedef struct {
    uint64_t sequence, frame, thread, hdc, path;
    uint64_t interval_begin, swap_begin, swap_end, first_render;
    uint64_t boundary_begin, boundary_end, boundary_calls;
    uint64_t sync_ticks[4], sync_calls[4], client_wait_result, sequence_end;
} present_timeline_record;

static present_timeline_record g_present_timeline_records[PRESENT_TIMELINE_CAPACITY]
    __attribute__((used));
static volatile LONG64 g_present_timeline_sequence;
static present_timeline_record g_present_timeline_pending;
static DWORD g_present_timeline_thread;
static unsigned g_present_timeline_depth;
static volatile LONG64 g_present_timeline_other_swaps;

#ifndef PRESENT_TIMELINE_CLOCK
static uint64_t present_timeline_clock(void)
{
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return (uint64_t)now.QuadPart;
}
#define PRESENT_TIMELINE_CLOCK() present_timeline_clock()
#endif

static int present_timeline_owner(void)
{
    return g_present_timeline_thread == GetCurrentThreadId();
}

static void present_timeline_first_render(void)
{
    if (!g_present_timeline_pending.first_render && present_timeline_owner())
        g_present_timeline_pending.first_render = PRESENT_TIMELINE_CLOCK();
}

static void present_timeline_boundary(int after)
{
    if (!present_timeline_owner()) return;
    if (after) g_present_timeline_pending.boundary_end = PRESENT_TIMELINE_CLOCK();
    else {
        g_present_timeline_pending.boundary_begin = PRESENT_TIMELINE_CLOCK();
        g_present_timeline_pending.boundary_calls++;
    }
}

static uint64_t present_timeline_sync_begin(void)
{
    return present_timeline_owner() ? PRESENT_TIMELINE_CLOCK() : 0;
}

static void present_timeline_sync_end(unsigned kind, uint64_t start, unsigned result)
{
    if (!start || kind >= 4) return;
    g_present_timeline_pending.sync_ticks[kind] += PRESENT_TIMELINE_CLOCK() - start;
    g_present_timeline_pending.sync_calls[kind]++;
    if (kind == PRESENT_TIME_CLIENT_WAIT)
        g_present_timeline_pending.client_wait_result = result;
}

static void present_timeline_swap_begin(HDC hdc, unsigned path)
{
    DWORD thread = GetCurrentThreadId();
    InterlockedCompareExchange((volatile LONG *)&g_present_timeline_thread,
                               (LONG)thread, 0);
    if (!present_timeline_owner()) {
        InterlockedIncrement64(&g_present_timeline_other_swaps);
        return;
    }
    if (g_present_timeline_depth++) return;
    g_present_timeline_pending.hdc = (uintptr_t)hdc;
    g_present_timeline_pending.path = path;
    g_present_timeline_pending.thread = thread;
    g_present_timeline_pending.swap_begin = PRESENT_TIMELINE_CLOCK();
}

static void present_timeline_swap_end(uint64_t frame)
{
    if (!present_timeline_owner() || !g_present_timeline_depth ||
        --g_present_timeline_depth) return;
    uint64_t end = PRESENT_TIMELINE_CLOCK();
    LONG64 sequence = g_present_timeline_sequence + 1;
    present_timeline_record *out = &g_present_timeline_records[
        (sequence - 1) % PRESENT_TIMELINE_CAPACITY];
    g_present_timeline_pending.frame = frame;
    g_present_timeline_pending.swap_end = end;
    InterlockedExchange64((volatile LONG64 *)&out->sequence, 0);
    InterlockedExchange64((volatile LONG64 *)&out->sequence_end, 0);
    memcpy(&out->frame, &g_present_timeline_pending.frame, sizeof(*out) - 16);
    InterlockedExchange64((volatile LONG64 *)&out->sequence_end, sequence);
    InterlockedExchange64((volatile LONG64 *)&out->sequence, sequence);
    InterlockedExchange64(&g_present_timeline_sequence, sequence);
    memset(&g_present_timeline_pending, 0, sizeof g_present_timeline_pending);
    g_present_timeline_pending.interval_begin = end;
}
#else
#define present_timeline_first_render() ((void)0)
#define present_timeline_boundary(after) ((void)0)
#define present_timeline_sync_begin() ((uint64_t)0)
#define present_timeline_sync_end(kind, start, result) ((void)0)
#define present_timeline_swap_begin(hdc, path) ((void)0)
#define present_timeline_swap_end(frame) ((void)0)
#endif
#endif
