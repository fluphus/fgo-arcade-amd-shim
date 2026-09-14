/* Isolated collector fixture; no game or graphics driver calls. */
#include <windows.h>
#include <stdint.h>
#ifdef FIXTURE_DLL
#define COUNTER(name) uint64_t name;
COUNTER(g_frame_count)
COUNTER(g_perf_pointer_exact_cache_hits)
COUNTER(g_perf_pointer_exact_cache_misses)
COUNTER(g_perf_pointer_shadow_reads)
COUNTER(g_perf_cpu_shadow_reads)
COUNTER(g_perf_mapped_flush_shadow_reads)
COUNTER(g_perf_mapped_flush_shadow_flushes)
COUNTER(g_perf_mapped_flush_shadow_bytes)
COUNTER(g_perf_mapped_flush_shadow_misses)
COUNTER(g_perf_pointer_replay_hash_cache_hits)
COUNTER(g_perf_pointer_replay_hash_cache_misses)
COUNTER(g_perf_emitter_header_hits)
COUNTER(g_perf_emitter_header_misses)
COUNTER(g_perf_rs_draws)
COUNTER(g_perf_rs_fallbacks)
COUNTER(g_perf_rs_handle_fallbacks)
COUNTER(g_perf_rs_ui_draws)
COUNTER(g_perf_rs_ui_fallbacks)
COUNTER(g_perf_rs_upload_hits)
COUNTER(g_perf_rs_upload_misses)
COUNTER(g_perf_rs_program_query_skips)
COUNTER(g_perf_rs_ubo_query_skips)
COUNTER(g_perf_rs_ubo_query_fallbacks)
COUNTER(g_bindless_state_replay_nv_range_updates)
COUNTER(g_bindless_state_replay_nv_range_redundant)
unsigned g_current_program = 1148;
__declspec(dllexport) void FixtureTick(void)
{
    ++g_frame_count;
    g_perf_rs_draws += 500;
}
#else
int main(void)
{
    HMODULE module = LoadLibraryW(L".\\opengl32.dll");
    if (!module) return 1;
    void (*tick)(void) = (void (*)(void))GetProcAddress(module, "FixtureTick");
    if (!tick) return 2;
    for (int i = 0; i < 1000; ++i) { tick(); Sleep(30); }
    return 0;
}
#endif
