#include <limits.h>

typedef struct {
    int32_t enabled, size, type, normalized, integer, relative, binding;
    int32_t buffer, stride, divisor;
    int64_t offset;
} UiWatchFetch;
typedef struct {
    uint64_t frame, tick;
    uint32_t program, vao, fbo, unified, mismatch;
    int32_t drawcount;
    UiWatchFetch live[2];
    FgoUiRequestedAttribute requested[16];
    LiveRange nv[16];
} UiWatchRecord;

static FgoUiCaptureConfig watch_config;
static const FgoUiCaptureApi *watch_capture;
static void (WINAPI *watch_attrib)(uint32_t, uint32_t, int32_t *);
static void (WINAPI *watch_binding)(uint32_t, uint32_t, int32_t *);
static void (WINAPI *watch_offset)(uint32_t, uint32_t, int64_t *);
static UiWatchRecord watch_ring[128];
static uint64_t watch_checked, watch_deadline;
static unsigned watch_triggered, watch_capture_started;

static void watch_phase_file(const char *name)
{
    char path[MAX_PATH];
    snprintf(path, sizeof path, "%sui_watch_%s.json", watch_config.directory, name);
    FILE *f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "{\"phase\":\"%s\",\"pid\":%u,\"tick\":%llu,\"checked\":%llu,"
        "\"triggered\":%u,\"deadline_tick\":%llu,\"program\":203,\"attributes\":[0,2]}\n",
        name, watch_config.pid, GetTickCount64(), watch_checked, watch_triggered, watch_deadline);
    fclose(f);
}

static void watch_save_ring(void)
{
    char path[MAX_PATH];
    snprintf(path, sizeof path, "%sui_watch_states.jsonl", watch_config.directory);
    FILE *f = fopen(path, "w");
    if (!f) return;
    uint64_t first = watch_checked > 128 ? watch_checked - 128 : 0;
    for (uint64_t n = first; n < watch_checked; n++) {
        const UiWatchRecord *r = &watch_ring[n % 128];
        fprintf(f, "{\"sample\":%llu,\"frame\":%llu,\"tick\":%llu,\"program\":%u,"
            "\"vao\":%u,\"fbo\":%u,\"unified\":%u,\"mismatch\":%u,\"drawcount\":%d,\"live\":[",
            n, r->frame, r->tick, r->program, r->vao, r->fbo, r->unified, r->mismatch, r->drawcount);
        for (unsigned i = 0; i < 2; i++) {
            const UiWatchFetch *a = &r->live[i];
            fprintf(f, "%s{\"attribute\":%u,\"enabled\":%d,\"size\":%d,\"type\":%d,"
                "\"normalized\":%d,\"integer\":%d,\"relative\":%d,\"binding\":%d,"
                "\"buffer\":%d,\"stride\":%d,\"divisor\":%d,\"offset\":%lld}",
                i ? "," : "", i * 2, a->enabled, a->size, a->type, a->normalized, a->integer,
                a->relative, a->binding, a->buffer, a->stride, a->divisor, a->offset);
        }
        fprintf(f, "],\"requested\":[");
        for (unsigned i = 0; i < 16; i++) {
            const FgoUiRequestedAttribute *a = &r->requested[i];
            fprintf(f, "%s{\"attribute\":%u,\"binding\":%d,\"size\":%u,\"type\":%u,"
                "\"normalized\":%u,\"integer\":%u,\"relative\":%u,\"enabled\":%u,"
                "\"keep_address\":%llu,\"keep_length\":%lld,\"nv_address\":%llu,\"nv_length\":%lld,"
                "\"binding_address\":%llu,\"binding_length\":%lld,\"buffer\":%u,\"stride\":%d,\"offset\":%lld}",
                i ? "," : "", i, a->binding, a->size, a->type, a->normalized, a->integer,
                a->relative, a->enabled, a->attribute_address, a->attribute_length,
                r->nv[i].addr, r->nv[i].len, a->binding_address, a->binding_length,
                a->core_buffer, a->core_stride, a->core_offset);
        }
        fprintf(f, "]}\n");
    }
    fclose(f);
}

/* Limit comparison to the interleaved UI layout already present in this game. */
static int watch_ui_layout(const FgoUiCaptureDraw *draw)
{
    if (*(const uint32_t *)(uintptr_t)request->address[STATE_PROGRAM] != 203) return 0;
    for (unsigned i = 0; i < 2; i++) {
        const FgoUiRequestedAttribute *a = &draw->requested[i * 2];
        if (!a->enabled || a->binding != 0 || a->size != (i ? 2u : 3u) ||
            a->type != 0x1406 || a->integer || a->relative != i * 16u || a->core_stride != 24)
            return 0;
        if (a->binding_address &&
            ((a->binding_address >> 48) != 0x4647 || a->core_offset != 0)) return 0;
    }
    return 1;
}

static void watch_read_fetch(unsigned index, UiWatchFetch *a)
{
    memset(a, 0, sizeof *a);
    watch_attrib(index, 0x8622, &a->enabled);
    watch_attrib(index, 0x8623, &a->size);
    watch_attrib(index, 0x8625, &a->type);
    watch_attrib(index, 0x886a, &a->normalized);
    watch_attrib(index, 0x88fd, &a->integer);
    watch_attrib(index, 0x82d5, &a->relative);
    watch_attrib(index, 0x82d4, &a->binding);
    if (a->binding < 0 || a->binding >= 16) return;
    watch_binding(0x8f4f, a->binding, &a->buffer);
    watch_binding(0x82d8, a->binding, &a->stride);
    watch_binding(0x82d6, a->binding, &a->divisor);
    watch_offset(0x82d7, a->binding, &a->offset);
}

static int watch_fetch_mismatch(const UiWatchFetch *live, const FgoUiRequestedAttribute *want)
{
    uint32_t buffer = want->core_buffer;
    int64_t offset = want->core_offset;
    if (want->binding_address) {
        buffer = (uint32_t)((want->binding_address >> 32) & 0xffffu);
        offset = (uint32_t)want->binding_address;
    }
    if (!live->enabled || live->binding < 0 || live->binding >= 16 ||
        live->size != (int32_t)want->size || live->type != (int32_t)want->type ||
        live->integer || live->buffer != (int32_t)buffer || live->stride != want->core_stride ||
        live->divisor || live->offset < 0 || live->relative < 0 ||
        live->offset > INT64_MAX - live->relative || offset < 0 || offset > INT64_MAX - want->relative)
        return 1;
    return live->offset + live->relative != offset + want->relative;
}

static int WINAPI live_watch_begin(const FgoUiCaptureConfig *input)
{
    watch_config = *input;
    watch_capture = FgoUiCaptureGetApi(FGO_UI_CAPTURE_VERSION);
    watch_attrib = (void *)input->resolve("glGetVertexAttribiv");
    watch_binding = (void *)input->resolve("glGetIntegeri_v");
    watch_offset = (void *)input->resolve("glGetInteger64i_v");
    if (!watch_capture || !watch_attrib || !watch_binding || !watch_offset) return 0;
    watch_deadline = GetTickCount64() + 20u * 60u * 1000u;
    watch_phase_file("armed");
    return 1;
}

static void WINAPI live_watch_multi(const FgoUiCaptureDraw *draw)
{
    if (watch_triggered) {
        if (watch_capture_started) watch_capture->multi(draw);
        return;
    }
    if (!watch_ui_layout(draw)) return;
    UiWatchRecord *r = &watch_ring[watch_checked % 128];
    r->frame = draw->game_frame; r->tick = GetTickCount64(); r->drawcount = draw->drawcount;
    r->program = *(const uint32_t *)(uintptr_t)request->address[STATE_PROGRAM];
    r->vao = *(const uint32_t *)(uintptr_t)request->address[STATE_VAO];
    r->fbo = *(const uint32_t *)(uintptr_t)request->address[STATE_FBO];
    r->unified = *(const uint32_t *)(uintptr_t)request->address[STATE_UNIFIED_ATTR];
    memcpy(r->requested, draw->requested, sizeof r->requested);
    memcpy(r->nv, (const void *)(uintptr_t)request->address[STATE_NV], sizeof r->nv);
    r->mismatch = 0;
    for (unsigned i = 0; i < 2; i++) {
        watch_read_fetch(i * 2, &r->live[i]);
        if (watch_fetch_mismatch(&r->live[i], &r->requested[i * 2])) r->mismatch |= 1u << (i * 2);
    }
    watch_checked++;
    if (!r->mismatch) return;
    watch_triggered = 1;
    watch_save_ring();
    watch_phase_file("triggered");
    watch_capture_started = watch_capture->begin(&watch_config);
    if (watch_capture_started) watch_capture->multi(draw);
}

static int live_watch_done(void)
{
    return watch_triggered || GetTickCount64() >= watch_deadline;
}

static int WINAPI live_watch_finish(HDC dc)
{
    int result = watch_capture_started ? watch_capture->finish(dc) : 2;
    if (!watch_triggered) watch_save_ring();
    watch_phase_file(watch_triggered ? "finished" : "expired");
    return result;
}

static const FgoUiCaptureApi *live_watch_api(void)
{
    static const FgoUiCaptureApi api = {FGO_UI_CAPTURE_VERSION,
        live_watch_begin, live_watch_multi, live_watch_finish};
    return &api;
}

__declspec(dllexport) DWORD WINAPI FgoUiStateWatchVersion(void)
{
    return 1;
}
