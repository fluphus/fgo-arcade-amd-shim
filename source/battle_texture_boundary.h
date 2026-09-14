/* Diagnostic synchronization at the early-stall API, never a render fix. */
static int g_battle_texture_boundary_on;
static GLsync (WINAPI *battle_texture_fence)(GLenum, GLbitfield);
static GLenum (WINAPI *battle_texture_wait)(GLsync, GLbitfield, GLuint64);
static void (WINAPI *battle_texture_delete)(GLsync);
static GLboolean (WINAPI *battle_texture_is_texture)(GLuint);
static GLboolean (WINAPI *battle_texture_is_sampler)(GLuint);
static void (WINAPI *battle_texture_level)(GLuint, GLint, GLenum, GLint *);
static void (WINAPI *battle_texture_parameter)(GLuint, GLenum, GLint *);
static void (WINAPI *battle_sampler_parameter)(GLuint, GLenum, GLint *);
static void (WINAPI *battle_sampler_parameter_f)(GLuint, GLenum, float *);
static int battle_texture_resolved;
static SRWLOCK g_battle_app_fence_lock = SRWLOCK_INIT;
static struct {
    GLsync sync;
    HGLRC context;
    DWORD thread;
    LONG64 sequence;
} g_battle_last_app_fence;

static void battle_texture_app_fence_created(GLsync sync, LONG64 sequence)
{
    if (!g_battle_texture_boundary_on || !g_battle_load_trace_on || !g_battle_observe_armed) return;
    DWORD saved_error = GetLastError();
    HGLRC context = battle_current_context ? battle_current_context() : NULL;
    AcquireSRWLockExclusive(&g_battle_app_fence_lock);
    g_battle_last_app_fence.sync = sync;
    g_battle_last_app_fence.context = context;
    g_battle_last_app_fence.thread = GetCurrentThreadId();
    g_battle_last_app_fence.sequence = sequence;
    ReleaseSRWLockExclusive(&g_battle_app_fence_lock);
    SetLastError(saved_error);
}

static void battle_texture_app_fence_deleted(GLsync sync)
{
    if (!g_battle_texture_boundary_on) return;
    DWORD saved_error = GetLastError();
    AcquireSRWLockExclusive(&g_battle_app_fence_lock);
    if (g_battle_last_app_fence.sync == sync)
        g_battle_last_app_fence.sync = NULL;
    ReleaseSRWLockExclusive(&g_battle_app_fence_lock);
    SetLastError(saved_error);
}

static void battle_texture_poll_app_fence(unsigned phase)
{
    DWORD saved_error = GetLastError();
    HGLRC context = battle_current_context ? battle_current_context() : NULL;
    /* App deletion cannot invalidate the tracked sync during this zero-timeout poll. */
    AcquireSRWLockExclusive(&g_battle_app_fence_lock);
    uint64_t id = (uint64_t)(uintptr_t)g_battle_last_app_fence.sync;
    uint64_t sequence = (uint64_t)g_battle_last_app_fence.sequence;
    if (id && context && context == g_battle_last_app_fence.context &&
        GetCurrentThreadId() == g_battle_last_app_fence.thread && battle_texture_wait) {
        battle_progress_event("btex_frame_poll_before", phase, (int)id, (int)(id >> 32),
                              (int)sequence, (int)(sequence >> 32));
        GLenum result = battle_texture_wait(g_battle_last_app_fence.sync, 0, 0);
        battle_progress_event("btex_frame_poll_after", result, (int)id, (int)(id >> 32),
                              (int)sequence, (int)(sequence >> 32));
    } else {
        battle_progress_event("btex_frame_unavailable", phase, (int)id, (int)(id >> 32),
                              (int)sequence, (int)(sequence >> 32));
    }
    ReleaseSRWLockExclusive(&g_battle_app_fence_lock);
    SetLastError(saved_error);
}

static void battle_texture_resolve(void)
{
    if (battle_texture_resolved) return;
    battle_texture_resolved = 1;
    if (!real_wglGetProcAddress) return;
#define BATTLE_TEXTURE_RESOLVE(slot, name) do { \
    PROC p = real_wglGetProcAddress(name); \
    if ((uintptr_t)p > 3 && p != (PROC)(intptr_t)-1) slot = (void *)p; \
} while (0)
    BATTLE_TEXTURE_RESOLVE(battle_texture_fence, "glFenceSync");
    BATTLE_TEXTURE_RESOLVE(battle_texture_wait, "glClientWaitSync");
    BATTLE_TEXTURE_RESOLVE(battle_texture_delete, "glDeleteSync");
    BATTLE_TEXTURE_RESOLVE(battle_texture_is_texture, "glIsTexture");
    BATTLE_TEXTURE_RESOLVE(battle_texture_is_sampler, "glIsSampler");
    BATTLE_TEXTURE_RESOLVE(battle_texture_level, "glGetTextureLevelParameteriv");
    BATTLE_TEXTURE_RESOLVE(battle_texture_parameter, "glGetTextureParameteriv");
    BATTLE_TEXTURE_RESOLVE(battle_sampler_parameter, "glGetSamplerParameteriv");
    BATTLE_TEXTURE_RESOLVE(battle_sampler_parameter_f, "glGetSamplerParameterfv");
#undef BATTLE_TEXTURE_RESOLVE
}

static void battle_texture_describe(GLuint texture, GLuint sampler)
{
    if (!battle_texture_is_texture || !battle_texture_is_sampler ||
        !battle_texture_level || !battle_texture_parameter ||
        !battle_sampler_parameter || !battle_sampler_parameter_f) {
        battle_progress_event("btex_desc_missing", 0, texture, sampler, 0, 0);
        return;
    }
    battle_progress_event("btex_desc_before", 0, texture, sampler, 0, 0);
    GLboolean valid_texture = battle_texture_is_texture(texture);
    GLboolean valid_sampler = battle_texture_is_sampler(sampler);
    battle_progress_event("btex_objects", 0, texture, sampler, valid_texture, valid_sampler);
    if (!valid_texture || !valid_sampler) return;
    GLint shape[4] = {0}, mips[4] = {0}, filter[4] = {0}, wrap[4] = {0};
    float lod[4] = {0};
    const GLenum shape_names[] = {0x1000, 0x1001, 0x8071, 0x1003};
    const GLenum mip_names[] = {0x813c, 0x813d, 0x82df, 0x912f};
    const GLenum filter_names[] = {0x2801, 0x2800, 0x884c, 0x884d};
    const GLenum wrap_names[] = {0x2802, 0x2803, 0x8072};
    const GLenum lod_names[] = {0x813a, 0x813b, 0x8501};
    for (unsigned i = 0; i < 4; i++)
        battle_texture_level(texture, 0, shape_names[i], &shape[i]);
    battle_progress_event("btex_shape", texture, shape[0], shape[1], shape[2], shape[3]);
    for (unsigned i = 0; i < 4; i++)
        battle_texture_parameter(texture, mip_names[i], &mips[i]);
    battle_progress_event("btex_mips", texture, mips[0], mips[1], mips[2], mips[3]);
    for (unsigned i = 0; i < 4; i++)
        battle_sampler_parameter(sampler, filter_names[i], &filter[i]);
    battle_progress_event("btex_filter", sampler, filter[0], filter[1], filter[2], filter[3]);
    for (unsigned i = 0; i < 3; i++)
        battle_sampler_parameter(sampler, wrap_names[i], &wrap[i]);
    battle_progress_event("btex_wrap", sampler, wrap[0], wrap[1], wrap[2], 0);
    for (unsigned i = 0; i < 3; i++)
        battle_sampler_parameter_f(sampler, lod_names[i], &lod[i]);
    GLint bits[4];
    memcpy(bits, lod, sizeof bits);
    battle_progress_event("btex_lod", sampler, bits[0], bits[1], bits[2], 0);
    battle_progress_event("btex_desc_after", 0, texture, sampler, 0, 0);
}

#include "battle_pass_completion.h"

static void battle_texture_boundary_before(GLuint texture, GLuint sampler, unsigned api, PROC real)
{
    if (!g_battle_texture_boundary_on || !g_battle_load_trace_on || !g_battle_observe_armed) return;
    DWORD saved_error = GetLastError();
    battle_progress_event("btex_enter", api, texture, sampler, 0, 0);
    battle_texture_resolve();
    battle_texture_poll_app_fence(0);
    battle_pass_poll();
    if (battle_texture_fence && battle_texture_wait && battle_texture_delete) {
        battle_progress_event("btex_fence_before", 0x9117, texture, sampler, 0, 0);
        GLsync sync = battle_texture_fence(0x9117, 0);
        uint64_t id = (uint64_t)(uintptr_t)sync;
        battle_progress_event("btex_fence_after", 0, texture, sampler, (int)id, (int)(id >> 32));
        if (sync) {
            battle_progress_event("btex_wait_before", 1, texture, sampler, (int)id, (int)(id >> 32));
            GLenum result = battle_texture_wait(sync, 1, 1000000000ULL);
            battle_progress_event("btex_wait_after", result, texture, sampler, (int)id, (int)(id >> 32));
            battle_texture_poll_app_fence(1);
            battle_pass_poll();
            battle_progress_event("btex_delete_before", 0, texture, sampler, (int)id, (int)(id >> 32));
            battle_texture_delete(sync);
            battle_progress_event("btex_delete_after", 0, texture, sampler, (int)id, (int)(id >> 32));
            if (result == 0x911a || result == 0x911c)
                battle_texture_describe(texture, sampler);
        }
    } else {
        battle_progress_event("btex_sync_missing", 0, texture, sampler, 0, 0);
    }
    uint64_t address = (uint64_t)(uintptr_t)real;
    battle_progress_event("btex_get_before", api, texture, sampler, (int)address, (int)(address >> 32));
    SetLastError(saved_error);
}

static void battle_texture_boundary_after(GLuint texture, GLuint sampler, unsigned api, GLuint64 handle)
{
    if (g_battle_texture_boundary_on && g_battle_load_trace_on && g_battle_observe_armed)
        battle_progress_event("btex_get_after", api, texture, sampler, (int)handle, (int)(handle >> 32));
}
