/* Separate low-rate app-sync history from the armed, high-rate CPU call tail. */
#include "battle_load_trace.h"
#define BATTLE_PROGRESS_CAPACITY 65536u
static battle_trace_header *g_battle_progress;
static int g_battle_progress_opened;
static int g_battle_observe_armed;
static HGLRC (WINAPI *battle_current_context)(void);
static void battle_driver_errors_arm(void);
static void battle_driver_errors_failure(GLsync sync, unsigned phase, GLenum result);

static LONG64 battle_progress_event(const char *event, GLenum type,
                                   int a, int b, int c, int d)
{
    if (!g_battle_load_trace_on) return 0;
    DWORD saved_error=GetLastError();
    LONG64 published=0;
    uint64_t context=0;
    if (!battle_current_context && g_real) {
        HMODULE provider=GetModuleHandleA("opengl32real.dll");
        if (!provider) provider=g_real;
        battle_current_context=(void *)GetProcAddress(provider,"wglGetCurrentContext");
    }
    if (battle_current_context) context=(uint64_t)(uintptr_t)battle_current_context();
    AcquireSRWLockExclusive(&g_battle_trace_lock);
    if (!g_battle_progress_opened) {
        g_battle_progress_opened=1;
        char path[MAX_PATH];
        snprintf(path,sizeof path,BATTLE_LOAD_TRACE_DIR "battle_progress_v1_%lu.bin",
                 (unsigned long)GetCurrentProcessId());
        HANDLE file=CreateFileA(path,GENERIC_READ|GENERIC_WRITE,
            FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,NULL,CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,NULL);
        if (file!=INVALID_HANDLE_VALUE) {
            DWORD size=sizeof(battle_trace_header)+BATTLE_PROGRESS_CAPACITY*sizeof(battle_trace_record);
            HANDLE mapping=CreateFileMappingA(file,NULL,PAGE_READWRITE,0,size,NULL);
            if (mapping) {
                g_battle_progress=(battle_trace_header *)MapViewOfFile(mapping,FILE_MAP_WRITE,0,0,size);
                CloseHandle(mapping);
            }
            CloseHandle(file);
        }
        if (g_battle_progress) {
            memset(g_battle_progress,0,sizeof *g_battle_progress);
            memcpy(g_battle_progress->magic,"BTLTv2",6);
            g_battle_progress->version=2;
            g_battle_progress->capacity=BATTLE_PROGRESS_CAPACITY;
            g_battle_progress->record_size=sizeof(battle_trace_record);
            g_battle_progress->pid=GetCurrentProcessId();
            g_battle_progress->start_tick=GetTickCount64();
            g_battle_progress->header_size=sizeof(battle_trace_header);
        }
    }
    if (g_battle_progress) {
        LONG64 seq=g_battle_progress->committed+1;
        battle_trace_record *records=(battle_trace_record *)(g_battle_progress+1);
        battle_trace_record *record=&records[(seq-1)%BATTLE_PROGRESS_CAPACITY];
        InterlockedExchange64(&record->sequence,0);
        record->tick=GetTickCount64(); record->frame=g_frame_count;
        record->thread=GetCurrentThreadId(); record->program=g_current_program;
        record->shader=0; record->type=type;
        record->a=a; record->b=b; record->c=c; record->d=d;
        record->framebuffer=g_bound_draw_framebuffer; record->vao=g_current_vao;
        memset(record->event,0,sizeof record->event);
        strncpy(record->event,event,sizeof record->event-1);
        memset(record->reserved,0,sizeof record->reserved);
        memcpy(record->reserved,&context,sizeof context);
        uint64_t fine=g_battle_trace_header?(uint64_t)g_battle_trace_header->committed:0;
        memcpy(record->reserved+8,&fine,sizeof fine);
        if (g_battle_observe_armed) g_battle_progress->flags|=8u;
        if (type==0x911d && !strcmp(event,"app_client_wait_after"))
            g_battle_progress->flags|=16u;
        InterlockedExchange64(&record->sequence,seq);
        InterlockedExchange64(&g_battle_progress->committed,seq);
        published=seq;
    }
    ReleaseSRWLockExclusive(&g_battle_trace_lock);
    SetLastError(saved_error);
    return published;
}

enum {
    BATTLE_AUX_DX_OPEN, BATTLE_AUX_DX_CLOSE, BATTLE_AUX_DX_REGISTER,
    BATTLE_AUX_DX_UNREGISTER, BATTLE_AUX_DX_LOCK, BATTLE_AUX_DX_UNLOCK,
    BATTLE_AUX_DX_SHARE, BATTLE_AUX_COPY_IMAGE, BATTLE_AUX_COPY_TEXTURE,
    BATTLE_AUX_BLIT, BATTLE_AUX_MP4_COPY, BATTLE_AUX_COUNT
};
static volatile LONG64 g_battle_aux_counts[BATTLE_AUX_COUNT][2];

/* Keep lifetime totals: resource-upload traffic can overwrite earlier DX events. */
static void battle_aux_event(unsigned kind, unsigned after, const char *event,
                             GLenum detail, uint64_t first, uint64_t second)
{
    if (!g_battle_load_trace_on) return;
    InterlockedIncrement64(&g_battle_aux_counts[kind][after]);
    battle_progress_event(event, detail, (int)first, (int)(first >> 32),
                           (int)second, (int)(second >> 32));
}

static void battle_aux_snapshot(void)
{
    if (!g_battle_load_trace_on) return;
    for (unsigned kind = 0; kind < BATTLE_AUX_COUNT; ++kind) {
        uint64_t begun = (uint64_t)InterlockedCompareExchange64(&g_battle_aux_counts[kind][0], 0, 0);
        uint64_t ended = (uint64_t)InterlockedCompareExchange64(&g_battle_aux_counts[kind][1], 0, 0);
        battle_progress_event("aux_totals", kind, (int)begun, (int)(begun >> 32),
                              (int)ended, (int)(ended >> 32));
    }
}

static void battle_aux_objects(const char *event, HANDLE device, GLint count, HANDLE *objects)
{
    if (!g_battle_load_trace_on || !objects) return;
    for (GLint i = 0; i < count; ++i) {
        uint64_t dev = (uintptr_t)device, object = (uintptr_t)objects[i];
        battle_progress_event(event, (GLenum)i, (int)dev, (int)(dev >> 32),
                              (int)object, (int)(object >> 32));
    }
}

static void battle_observe_arm(void)
{
    if (!g_battle_load_trace_on || g_battle_observe_armed) return;
    DWORD saved_error=GetLastError();
    if (GetAsyncKeyState(VK_F8)&0x8000) {
        g_battle_observe_armed=1;
        battle_load_trace_event("armed",g_frame_count,g_current_program,0,0,0,0,0,0);
        battle_progress_event("armed",0,0,0,0,0);
        battle_driver_errors_arm();
    }
    SetLastError(saved_error);
}

#undef BATTLE_TRACE_DRAW
#define BATTLE_TRACE_DRAW(event, mode, a, b, c, d) do { \
    if (g_battle_load_trace_on && g_battle_observe_armed) \
        battle_load_trace_event(event,g_frame_count,g_current_program,0,mode,a,b,c,d); \
} while (0)
#define BATTLE_OBSERVE_CALL(event, mode, a, b, c, d, call) do { \
    BATTLE_TRACE_DRAW(event "_before",mode,a,b,c,d); \
    int battle_frame_active = battle_frame_entry_before(event); \
    unsigned battle_resource_active = battle_resource_before(event,mode,a,b,c,d); \
    call; \
    BATTLE_TRACE_DRAW(event "_after",mode,a,b,c,d); \
    battle_resource_after(battle_resource_active); \
    battle_frame_entry_after(battle_frame_active); \
    battle_pass_after_call(event); \
} while (0)

static void battle_observe_texture_handle(const char *event, unsigned api,
    GLuint texture, GLuint sampler, GLuint64 handle)
{
    if (!g_battle_load_trace_on) return;
    BATTLE_TRACE_DRAW(event, api, (int)texture, (int)sampler,
                      (int)handle, (int)(handle >> 32));
    battle_progress_event(event, api, (int)texture, (int)sampler,
                           (int)handle, (int)(handle >> 32));
}

static GLsync (WINAPI *battle_trace_real_fence)(GLenum,GLbitfield);
static GLenum (WINAPI *battle_trace_real_client_wait)(GLsync,GLbitfield,GLuint64);
static void (WINAPI *battle_trace_real_wait)(GLsync,GLbitfield,GLuint64);
static void (WINAPI *battle_trace_real_delete)(GLsync);
static void battle_texture_app_fence_created(GLsync sync, LONG64 sequence);
static void battle_texture_app_fence_deleted(GLsync sync);

static GLsync WINAPI battle_trace_fence(GLenum condition,GLbitfield flags)
{
    battle_progress_event("app_fence_before",condition,flags,0,0,0);
    GLsync result=battle_trace_real_fence(condition,flags);
    uint64_t id=(uint64_t)(uintptr_t)result;
    LONG64 sequence=battle_progress_event("app_fence_after",condition,flags,(int)id,(int)(id>>32),0);
    battle_texture_app_fence_created(result,sequence);
    return result;
}
static GLenum WINAPI battle_trace_client_wait(GLsync sync,GLbitfield flags,GLuint64 timeout)
{
    uint64_t id=(uint64_t)(uintptr_t)sync;
    battle_progress_event("app_client_wait_before",flags,(int)id,(int)(id>>32),(int)timeout,(int)(timeout>>32));
    GLenum result=battle_trace_real_client_wait(sync,flags,timeout);
    battle_progress_event("app_client_wait_after",result,(int)id,(int)(id>>32),(int)timeout,(int)(timeout>>32));
    if (result==0x911d && g_battle_observe_armed) battle_load_trace_freeze();
    return result;
}
static void WINAPI battle_trace_wait(GLsync sync,GLbitfield flags,GLuint64 timeout)
{
    uint64_t id=(uint64_t)(uintptr_t)sync;
    battle_progress_event("app_wait_before",flags,(int)id,(int)(id>>32),(int)timeout,(int)(timeout>>32));
    battle_trace_real_wait(sync,flags,timeout);
    battle_progress_event("app_wait_after",flags,(int)id,(int)(id>>32),(int)timeout,(int)(timeout>>32));
}
static void WINAPI battle_trace_delete(GLsync sync)
{
    uint64_t id=(uint64_t)(uintptr_t)sync;
    battle_progress_event("app_delete_before",0,(int)id,(int)(id>>32),0,0);
    battle_texture_app_fence_deleted(sync);
    battle_trace_real_delete(sync);
    battle_progress_event("app_delete_after",0,(int)id,(int)(id>>32),0,0);
}
static PROC battle_observe_lookup(const char *name)
{
    if (!g_battle_load_trace_on) return NULL;
    int which=!strcmp(name,"glFenceSync")?1:!strcmp(name,"glClientWaitSync")?2:
        !strcmp(name,"glWaitSync")?3:!strcmp(name,"glDeleteSync")?4:0;
    if (!which) return NULL;
    PROC p=real_wglGetProcAddress(name);
    if ((uintptr_t)p<=3 || p==(PROC)(intptr_t)-1) return p;
    if (which==1) { battle_trace_real_fence=(void *)p; return (PROC)battle_trace_fence; }
    if (which==2) { battle_trace_real_client_wait=(void *)p; return (PROC)battle_trace_client_wait; }
    if (which==3) { battle_trace_real_wait=(void *)p; return (PROC)battle_trace_wait; }
    battle_trace_real_delete=(void *)p; return (PROC)battle_trace_delete;
}

#include "battle_texture_boundary.h"
#include "battle_frame_entry.h"
#include "battle_resource_boundary.h"
#include "battle_driver_errors.h"
