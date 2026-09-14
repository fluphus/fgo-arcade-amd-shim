/* Included after the renderer's NV state definitions. Dormant draws do no work. */
static HANDLE g_ui_v2_mapping;
static FgoUiControl *g_ui_v2_control;
static unsigned g_ui_v2_present_depth;
static uint64_t g_ui_v2_presentation;
static int g_ui_v2_attempted, g_ui_v2_initialized;

static void ui_v2_phase(const char *phase, LONG status, DWORD error)
{
    if (g_ui_v2_control) g_ui_v2_control->status = status;
    char path[MAX_PATH], line[384];
    snprintf(path, sizeof path, FGO_UI_CAPTURE_DIR "battle_ui_%lu.phases.jsonl",
             (unsigned long)GetCurrentProcessId());
    int n = snprintf(line, sizeof line,
        "{\"phase\":\"%s\",\"pid\":%lu,\"presentation\":%llu,\"tick\":%llu,\"win32_error\":%lu}\n",
        phase, (unsigned long)GetCurrentProcessId(),
        (unsigned long long)g_ui_v2_presentation,
        (unsigned long long)GetTickCount64(), (unsigned long)error);
    HANDLE file = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_DELETE,
                             NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file != INVALID_HANDLE_VALUE) {
        DWORD written;
        WriteFile(file, line, (DWORD)n, &written, NULL);
        CloseHandle(file);
    }
}

static PROC WINAPI ui_v2_resolve(const char *name) { return trace_resolve(name); }

static int WINAPI ui_v2_lookup(uint64_t handle, uint32_t *texture, uint32_t *sampler)
{
    bindless_handle_rec *r = bindless_handle_find(handle);
    if (!r || !r->texture) return 0;
    *texture = r->texture;
    *sampler = r->sampler;
    return 1;
}

static void battle_ui_v2_multi(GLenum mode, const GLsizei *count, GLenum type,
    const void *const *indices, GLsizei drawcount, const GLint *basevertex)
{
    DWORD saved = GetLastError();
    FgoUiCaptureDraw draw = {0};
    draw.mode = mode; draw.counts = count; draw.index_type = type;
    draw.indices = indices; draw.drawcount = drawcount;
    draw.basevertices = basevertex; draw.game_frame = g_frame_count;
    for (unsigned a = 0; a < 16 && a < NV_MAX_ATTRIBS; a++) {
        FgoUiRequestedAttribute *r = &draw.requested[a];
        r->binding = g_abind[a].set ? (int32_t)g_abind[a].binding : -1;
        r->size = g_fmt[a].size; r->type = g_fmt[a].type;
        r->normalized = g_fmt[a].normalized; r->integer = g_fmt[a].is_int;
        r->relative = g_fmt[a].relativeoffset;
        r->enabled = !!(g_enabled_mask & (1u << a));
        r->attribute_address = g_nv_va_keep[a].addr;
        r->attribute_length = g_nv_va_keep[a].len;
        unsigned b = r->binding >= 0 ? (unsigned)r->binding : a;
        if (b < NV_MAX_ATTRIBS) {
            r->binding_address = g_nv_va[b].addr;
            r->binding_length = g_nv_va[b].len;
            r->core_buffer = g_vbuf[b].buffer;
            r->core_stride = g_vbuf[b].stride;
            r->core_offset = g_vbuf[b].offset;
        }
    }
    g_ui_v2_api->multi(&draw);
    SetLastError(saved);
}

static void battle_ui_v2_before_present(HDC dc)
{
    if (g_ui_v2_present_depth++ || !g_ui_v2_api) return;
    DWORD saved = GetLastError();
    const FgoUiCaptureApi *api = g_ui_v2_api;
    g_ui_v2_api = NULL;
    ui_v2_phase("finishing", FGO_UI_CAPTURING, 0);
    int result = api->finish(dc);
    ui_v2_phase(result == 1 ? "complete" : result == 2 ? "partial" : "failed",
                result == 1 ? FGO_UI_COMPLETE : result == 2 ? FGO_UI_PARTIAL : FGO_UI_FAILED, 0);
    SetLastError(saved);
}

static void battle_ui_v2_after_present(HDC dc)
{
    if (!g_ui_v2_present_depth || --g_ui_v2_present_depth) return;
    if (g_ui_v2_attempted) return;
    DWORD saved = GetLastError();
    g_ui_v2_presentation++;
    if (!g_ui_v2_initialized) {
        g_ui_v2_initialized = 1;
        char name[96];
        snprintf(name, sizeof name, "Local\\FgoBattleUIV2_%lu", (unsigned long)GetCurrentProcessId());
        g_ui_v2_mapping = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
                                           0, sizeof(FgoUiControl), name);
        if (g_ui_v2_mapping)
            g_ui_v2_control = MapViewOfFile(g_ui_v2_mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(FgoUiControl));
        if (!g_ui_v2_control) {
            ui_v2_phase("control_failed", FGO_UI_FAILED, GetLastError());
            g_ui_v2_attempted = 1;
            goto out;
        }
        memset(g_ui_v2_control, 0, sizeof *g_ui_v2_control);
        g_ui_v2_control->version = FGO_UI_CAPTURE_VERSION;
        g_ui_v2_control->pid = GetCurrentProcessId();
        g_ui_v2_control->status = FGO_UI_READY;
        MemoryBarrier();
        g_ui_v2_control->magic = FGO_UI_CAPTURE_MAGIC;
        ui_v2_phase("ready", FGO_UI_READY, 0);
    }
    g_ui_v2_control->presentation = g_ui_v2_presentation;
    if (!g_ui_v2_control->request) goto out;
    g_ui_v2_attempted = 1;
    ui_v2_phase("requested", FGO_UI_REQUESTED, 0);
    HMODULE module = LoadLibraryA(FGO_UI_CAPTURE_DLL);
    if (!module) { ui_v2_phase("load_failed", FGO_UI_FAILED, GetLastError()); goto out; }
    ui_v2_phase("collector_loaded", FGO_UI_LOADED, 0);
    FgoUiGetApi get_api = (FgoUiGetApi)GetProcAddress(module, "FgoUiCaptureGetApi");
    const FgoUiCaptureApi *api = get_api ? get_api(FGO_UI_CAPTURE_VERSION) : NULL;
    if (!api || api->version != FGO_UI_CAPTURE_VERSION || !api->begin || !api->multi || !api->finish) {
        ui_v2_phase("interface_failed", FGO_UI_FAILED, ERROR_BAD_FORMAT);
        goto out;
    }
    FgoUiCaptureConfig config = { FGO_UI_CAPTURE_VERSION, GetCurrentProcessId(),
        g_ui_v2_presentation + 1, dc, FGO_UI_CAPTURE_DIR, ui_v2_resolve, ui_v2_lookup };
    if (!api->begin(&config)) { ui_v2_phase("begin_failed", FGO_UI_FAILED, 0); goto out; }
    g_ui_v2_api = api;
    ui_v2_phase("capturing", FGO_UI_CAPTURING, 0);
out:
    SetLastError(saved);
}
