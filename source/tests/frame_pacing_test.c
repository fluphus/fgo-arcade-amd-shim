#include "../shim.c"
#include <assert.h>

static void exact_second_checks(void)
{
    const LONGLONG frequencies[] = {60, 1000000, 10000000, 10000019, 19200000};
    unsigned f;
    for (f = 0; f < sizeof frequencies / sizeof frequencies[0]; ++f) {
        const LONGLONG hz = frequencies[f];
        const LONGLONG start = 100 * hz;
        pace_anchor s = {0};
        LONGLONG now = start;
        unsigned frame;
        assert(pace_anchor_deadline(&s, now, hz) == now);
        pace_anchor_release(&s, now, hz);
        for (frame = 0; frame < 600; ++frame) {
            LONGLONG deadline = pace_anchor_deadline(&s, now, hz);
            LONGLONG spacing = deadline - now;
            assert(spacing == hz / 60 || spacing == hz / 60 + 1);
            now = deadline;
            pace_anchor_release(&s, now, hz);
        }
        assert(now - start == 10 * hz);
    }
    puts("PASS 600 on-time frames span exactly 10 seconds");
}

static void no_catchup_checks(void)
{
    const LONGLONG hz = 10000000, start = 100 * hz;
    pace_anchor s = {0};
    LONGLONG now = start;
    unsigned frame;
    LONGLONG elapsed, base, overshoot;
    pace_anchor_release(&s, now, hz);
    for (frame = 0; frame < 600; ++frame) {
        LONGLONG deadline = pace_anchor_deadline(&s, now, hz);
        LONGLONG spacing = deadline - now;
        assert(spacing == hz / 60 || spacing == hz / 60 + 1);
        now = deadline + hz / 5000; /* 0.2 ms wake overshoot */
        pace_anchor_release(&s, now, hz);
    }
    elapsed = now - start;
    base = 600 * (hz / 60);
    overshoot = 600 * (hz / 5000);
    assert(elapsed >= base + overshoot);
    assert(elapsed <= base + overshoot + 600);
    printf("wake_overshoot_retained_ms=%.3f\n",
           1000.0 * (double)(elapsed - base) / (double)hz);

    /* A pause rebases once. The following frame is one full period, not a burst. */
    {
        LONGLONG late = now + 4 * hz;
        LONGLONG next;
        assert(pace_anchor_deadline(&s, late, hz) == late);
        pace_anchor_release(&s, late, hz);
        next = pace_anchor_deadline(&s, late, hz);
        assert(next - late == hz / 60 || next - late == hz / 60 + 1);
        for (frame = 0; frame < 100; ++frame) {
            LONGLONG arrival = late + (LONGLONG)(frame + 1) * (hz / 25);
            LONGLONG spacing;
            assert(pace_anchor_deadline(&s, arrival, hz) == arrival);
            pace_anchor_release(&s, arrival, hz);
            spacing = pace_anchor_deadline(&s, arrival, hz) - arrival;
            assert(spacing == hz / 60 || spacing == hz / 60 + 1);
        }
    }
    puts("PASS late frames keep a full next interval");
}

static void cliff_checks(void)
{
    const LONGLONG hz = 10000000, start = 100 * hz;
    LONGLONG late;
    for (late = hz / 1000; late <= hz / 1000 + 1; ++late) {
        pace_anchor s = {0};
        LONGLONG deadline, release, spacing;
        pace_anchor_release(&s, start, hz);
        deadline = pace_anchor_deadline(&s, start, hz);
        release = deadline + late;
        assert(pace_anchor_deadline(&s, release, hz) == release);
        pace_anchor_release(&s, release, hz);
        spacing = pace_anchor_deadline(&s, release, hz) - release;
        assert(spacing == hz / 60 || spacing == hz / 60 + 1);
        printf("late_ms=%.4f next_spacing_ms=%.4f\n",
               1000.0 * (double)late / (double)hz,
               1000.0 * (double)spacing / (double)hz);
    }
    puts("PASS no 1 ms spacing cliff");
}

static void deadline_bound_checks(void)
{
    const LONGLONG frequencies[] = {60, 1000000, 10000000, 10000019, 19200000};
    unsigned comparisons = 0;
    unsigned f;
    for (f = 0; f < sizeof frequencies / sizeof frequencies[0]; ++f) {
        const LONGLONG hz = frequencies[f], start = 100 * hz;
        const LONGLONG lateness[] = {
            0, 1, hz / 10000, hz / 2000, hz / 1000, hz / 1000 + 1,
            hz / 60, hz / 60 + 1, hz / 25, 4 * hz
        };
        unsigned r, i, work;
        for (r = 0; r < 60; ++r) {
            for (i = 0; i < sizeof lateness / sizeof lateness[0]; ++i) {
                pace_anchor s = {0};
                LONGLONG arrival = start + lateness[i];
                LONGLONG legacy = start + hz / 60;
                LONGLONG shown, spacing;
                s.armed = 1;
                s.frequency = hz;
                s.origin = start;
                s.remainder = r;
                shown = pace_anchor_deadline(&s, arrival, hz);
                if (arrival < legacy) {
                    assert(shown >= legacy);
                    assert(shown <= legacy + 1);
                } else if (arrival == legacy) {
                    assert(shown == arrival || shown == arrival + 1);
                } else {
                    assert(shown == arrival);
                }
                for (work = 0; work <= 40; ++work) {
                    pace_anchor probe = s;
                    LONGLONG at = start + (LONGLONG)work * hz / 1000;
                    LONGLONG d = pace_anchor_deadline(&probe, at, hz);
                    LONGLONG new_wait = d > at ? d - at : 0;
                    LONGLONG legacy_wait = legacy > at ? legacy - at : 0;
                    assert(new_wait <= legacy_wait + 1);
                    if (at < legacy)
                        assert(new_wait >= legacy_wait);
                    ++comparisons;
                }
                pace_anchor_release(&s, arrival, hz);
                spacing = pace_anchor_deadline(&s, arrival, hz) - arrival;
                assert(spacing == hz / 60 || spacing == hz / 60 + 1);
                assert(s.remainder < 60);
                ++comparisons;
            }
        }
    }

    {
        const LONGLONG hz = 10000000;
        pace_anchor s = {0};
        assert(pace_anchor_deadline(&s, 123, hz) == 123);
        pace_anchor_release(&s, 123, hz);
        assert(pace_anchor_deadline(&s, 50, hz) == 50);
        pace_anchor_release(&s, 50, hz);
        assert(s.remainder == 0);
        assert(pace_anchor_deadline(&s, 456, hz * 2) == 456);
        pace_anchor_release(&s, 456, hz * 2);
        assert(s.frequency == hz * 2);
        assert(s.remainder == 0);
    }
    printf("PASS deadline bounds: %u comparisons, extra wait <= one QPC tick\n", comparisons);
}

static void measure(int legacy)
{
    enum { N = 240 };
    LARGE_INTEGER f, now, start, last;
    int i;
    double min_ms = 1e9, max_ms = 0, elapsed;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&last);
    if (!legacy) pace_frame_60hz();
    QueryPerformanceCounter(&start);
    for (i = 0; i < N; ++i) {
        LONGLONG before = last.QuadPart;
        double ms;
        if (legacy) {
            QueryPerformanceCounter(&now);
            pace_wait_until(&now, last.QuadPart + f.QuadPart / 60, f.QuadPart);
            last = now;
        } else {
            pace_frame_60hz();
            QueryPerformanceCounter(&last);
        }
        ms = 1000.0 * (double)(last.QuadPart - before) / (double)f.QuadPart;
        if (i && ms < min_ms) min_ms = ms;
        if (i && ms > max_ms) max_ms = ms;
    }
    elapsed = (double)(last.QuadPart - start.QuadPart) / (double)f.QuadPart;
    printf("%s frames=%d seconds=%.6f fps=%.5f min_ms=%.5f max_ms=%.5f drift_ms=%.5f\n",
           legacy ? "legacy" : "fractional", N, elapsed, N / elapsed,
           min_ms, max_ms, 1000.0 * (elapsed - N / 60.0));
}

int main(int argc, char **argv)
{
    g_telemetry_quiet = 1;
    exact_second_checks();
    no_catchup_checks();
    cliff_checks();
    deadline_bound_checks();
    if (argc > 1 && !strcmp(argv[1], "--timing")) {
        measure(1);
        measure(0);
    }
    return 0;
}