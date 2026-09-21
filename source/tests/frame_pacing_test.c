#include "../shim.c"
#include <assert.h>

static void deterministic_checks(void)
{
    const LONGLONG start = 100000000, hz = 10000000;
    pace_schedule s = {0};
    assert(pace_schedule_deadline(&s, start, hz) == start);
    for (unsigned frame = 0; frame < 600; ++frame) {
        LONGLONG ideal = start + (LONGLONG)frame * hz / 60;
        assert(s.deadline == ideal);
        pace_schedule_release(&s, ideal + hz / 5000); /* 0.2 ms wake error */
    }
    assert(s.deadline == start + 10 * hz);
    printf("constant_0.2ms_wake_error: legacy_drift_ms=120 absolute_drift_ms=0.2\n");

    /* A long pause rebases once: the next frame is a full period later. */
    LONGLONG late = s.deadline + 4 * hz;
    assert(pace_schedule_deadline(&s, late, hz) < late);
    pace_schedule_release(&s, late);
    assert(s.deadline == late + hz / 60);
    assert(pace_schedule_deadline(&s, late, hz) > late);

    /* Small late arrival caused by VSync consumes the wait; it does not
       restart another full 16.67 ms sleep from the return time. */
    memset(&s, 0, sizeof s);
    pace_schedule_deadline(&s, start, hz);
    pace_schedule_release(&s, start);
    for (unsigned frame = 1; frame <= 600; ++frame) {
        LONGLONG arrival = start + (LONGLONG)frame * hz / 60 + hz / 10000;
        assert(pace_schedule_deadline(&s, arrival, hz) <= arrival);
        pace_schedule_release(&s, arrival);
    }
    assert(s.deadline == start + 601 * hz / 60);

    /* Slow rendering must not produce a backlog of catch-up frames. */
    for (unsigned frame = 0; frame < 100; ++frame) {
        LONGLONG arrival = s.last + hz / 25;
        assert(pace_schedule_deadline(&s, arrival, hz) < arrival);
        pace_schedule_release(&s, arrival);
        assert(s.deadline == arrival + hz / 60);
    }
    assert(pace_schedule_deadline(&s, 123, hz) == 123);
    pace_schedule_release(&s, 123);
    assert(pace_schedule_deadline(&s, 456, hz * 2) == 456);
    puts("PASS exact fractional periods, bounded jitter recovery, VSync credit, stalls, clock reset");
}

static void measure(int legacy)
{
    enum { N = 240 };
    LARGE_INTEGER f, now, start, last;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&last);
    if (!legacy) pace_frame_60hz();
    QueryPerformanceCounter(&start);
    double min_ms = 1e9, max_ms = 0;
    for (int i = 0; i < N; ++i) {
        LONGLONG before = last.QuadPart;
        if (legacy) {
            QueryPerformanceCounter(&now);
            pace_wait_until(&now, last.QuadPart + f.QuadPart / 60, f.QuadPart);
            last = now;
        } else {
            pace_frame_60hz();
            QueryPerformanceCounter(&last);
        }
        double ms = 1000.0 * (last.QuadPart - before) / f.QuadPart;
        if (i && ms < min_ms) min_ms = ms;
        if (i && ms > max_ms) max_ms = ms;
    }
    double elapsed = (double)(last.QuadPart - start.QuadPart) / f.QuadPart;
    printf("%s frames=%d seconds=%.6f fps=%.5f min_ms=%.5f max_ms=%.5f drift_ms=%.5f\n",
           legacy ? "legacy" : "absolute", N, elapsed, N / elapsed,
           min_ms, max_ms, 1000.0 * (elapsed - N / 60.0));
}

int main(int argc, char **argv)
{
    g_telemetry_quiet = 1;
    deterministic_checks();
    if (argc > 1 && !strcmp(argv[1], "--timing")) {
        measure(1);
        measure(0);
    }
    return 0;
}
