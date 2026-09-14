#define main legacy_emitter_test_main
#include "per_emitter_pointer_test.c"
#undef main

static void battle_case(GLuint program, int four, int mask, int pingpong)
{
    uint64_t *header = audit_headers[pingpong];
    header[0] = make_fake_addr(628, 0);
    header[1] = mask & 1 ? 0 : make_fake_addr(629, pingpong ? 224 : 0);
    header[2] = mask & 2 ? 0 : make_fake_addr(629, 1024);
    header[3] = mask & 4 ? 0 : make_fake_addr(629, 2048);
    ++g_buffer_write_serial[630 + pingpong];
    trace_core_ubo_bind(0, 630 + pingpong, 0, 32);
    g_current_program = program;
    printf("battle_case program=%u slots=%d mask=%d pingpong=%d\n", program, four ? 4 : 3, mask, pingpong);
    apply_pointer_bindings();
    audit_expect(30, 628, 0);
    audit_expect(31, mask & 2 ? 0 : 629, 1024);
    if (four) audit_expect(32, mask & 4 ? 0 : 629, 2048);
    else if (g_prog_ptrs[program].emitter_struct_count) {
        audit_expect(32, mask & 1 ? 0 : 629, pingpong ? 240 : 16);
        audit_expect(33, mask & 1 ? 0 : 629, pingpong ? 244 : 20);
    }
    unsigned before = audit_bind_calls;
    apply_pointer_bindings();
    if (g_perf_pointer_ssbo_dedup_on && before != audit_bind_calls) ++audit_failures;
}

int main(int argc, char **argv)
{
    if (argc != 2 && argc != 3) return 2;
    audit_output_dir = argv[1];
    g_telemetry_quiet = 1; g_pointer_ubo_layout_fix_on = 1;
    g_perf_pointer_scan_key_on = g_perf_pointer_scan_cache_v2_on = 1;
    g_perf_pointer_scan_stack_on = g_perf_pointer_ubo_trim_on = 1;
    g_perf_emitter_header_cache_on = 1;
    g_ssbo_dumped = 4;
    real_glShaderSource = audit_shader_source;
    real_glBindBufferRange = audit_bind_range; real_glBindBufferBase = audit_bind_base;
    real_glBindBuffersRange = audit_bind_ranges; real_glGetNamedBufferSubData = audit_read_buffer;
    const GLuint shaders[] = {2625, 2631, 2633, 2635};
    for (unsigned i = 0; i < sizeof shaders / sizeof shaders[0]; ++i) {
        char path[1024];
        snprintf(path, sizeof path, "%s/native_%u.glsl",
                 argc == 3 ? argv[2] : "build/battle-translation-audit-20260909", shaders[i]);
        if (!audit_translate(shaders[i], 0x8b31, path)) return 3;
        audit_link_metadata(shaders[i] + 1, shaders[i]);
        if (!g_shader_ptrs[shaders[i]].per_emitter_pointer_layout) ++audit_failures;
        if (g_shader_ptrs[shaders[i]].m[0].ubo_byte_offset != 0 ||
            g_shader_ptrs[shaders[i]].m[1].ubo_byte_offset != 16) ++audit_failures;
        if (i == 0 && g_shader_ptrs[shaders[i]].m[2].ubo_byte_offset != 24) ++audit_failures;
        g_ptr_exact_probe_seen[shaders[i] + 1] = 1;
    }
    g_buffer_size[628] = 148800; g_buffer_size[629] = sizeof audit_pool;
    g_buffer_size[630] = g_buffer_size[631] = 32;
    uint64_t list0[] = {make_fake_addr(629, 16), make_fake_addr(629, 20)};
    uint64_t list1[] = {make_fake_addr(629, 240), make_fake_addr(629, 244)};
    memcpy(audit_pool, list0, sizeof list0); memcpy(audit_pool + 224, list1, sizeof list1);
    audit_layout_guard();
    const char *invalid[] = {
        "uint prefix; Particle* g_particles; Unused* g_unused0; float* m_randoms;",
        "Particle* g_particles; Unused* g_unused0; float* m_randoms; uint tail;",
        "Particle* g_particles; Unused* g_unused0; vec4* g_candidate_cells; float* g_randoms;"
    };
    for (unsigned i = 0; i < sizeof invalid / sizeof invalid[0]; ++i)
        if (per_emitter_pointer_block_matches(invalid[i], (int)strlen(invalid[i]))) ++audit_failures;
    for (int fast = 0; fast < 2; ++fast) {
        g_perf_pointer_replay_fastpath_on = g_perf_pointer_replay_general_on = fast;
        g_perf_pointer_replay_ssbo_key_on = g_perf_pointer_ssbo_batch_on = fast;
        g_perf_pointer_ssbo_dedup_on = fast;
        for (unsigned i = 0; i < sizeof shaders / sizeof shaders[0]; ++i)
            for (int mask = 0; mask < 5; ++mask)
                for (int pingpong = 0; pingpong < 2; ++pingpong)
                    battle_case(shaders[i] + 1, i == 0, mask, pingpong);
    }
    printf("BATTLE_EMITTER failures=%d submissions=%u\n", audit_failures, audit_submissions);
    return audit_failures ? 1 : 0;
}
