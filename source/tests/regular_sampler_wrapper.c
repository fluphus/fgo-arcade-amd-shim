#include "../shim.c"
#include <stddef.h>
_Static_assert(sizeof(perf_rs_sampler)==64,"sampler reader layout changed");
_Static_assert(offsetof(perf_rs_sampler,type)==60,"sampler type offset changed");
_Static_assert(offsetof(perf_rs_program,sampler_units)==4716,"sampler unit offset changed");
__declspec(dllexport) char *TestRegularSource(const char *source)
{
    return perf_rs_source(source);
}
__declspec(dllexport) void TestRegularFree(void *source) { free(source); }
__declspec(dllexport) int TestRegularUniformLayoutEqual(GLuint original, GLuint index,
    GLuint variant, GLuint other)
{
    if (!perf_rs_api()) return -1;
    return perf_rs_uniform_layout_equal(original,index,variant,other);
}
__declspec(dllexport) int TestRegularUniformIndices(GLuint program, GLsizei count,
    const char *const *texts, GLuint *indices, int cached)
{
    if (!perf_rs_api()) return -1;
    GLint name_count=0;
    perf_rs_uniform_name *names=cached ? perf_rs_uniform_names(program,&name_count) : NULL;
    int misses=0;
    for (GLsizei i=0;i<count;i++) {
        indices[i]=perf_rs_uniform_name_index(names,name_count,texts[i]);
        if (indices[i]==~0u) {
            g_perf_rs_gl.indices(program,1,&texts[i],&indices[i]);
            misses++;
        }
    }
    free(names);
    return misses;
}
__declspec(dllexport) int TestRegularInstall(HMODULE module, wglGetProcAddress_t resolve,
    GLuint program, const char *vs, const char *fs)
{
    g_real=module; real_wglGetProcAddress=resolve; g_telemetry_quiet=1;
    g_perf_cpu_shadow_on=g_perf_pointer_shadow_on=g_perf_regular_samplers_on=1;
    g_prog_shader_count[program]=2;
    g_prog_shaders[program][0]=60000; g_prog_shaders[program][1]=60001;
    g_shader_types[60000]=0x8B31; g_shader_types[60001]=0x8B30;
    store_shader_src(60000,vs,(int)strlen(vs)); store_shader_src(60001,fs,(int)strlen(fs));
    perf_rs_link(program);
    return g_perf_rs_programs[program] != NULL;
}

/* Seed the wrapper-owned mirrors from the real context after a test has
   completed its ordinary setup. Subsequent TestRegularDraw calls then exercise
   the production query-cache branch without bypassing its validity checks. */
__declspec(dllexport) void TestRegularSeedState(GLuint program)
{
    perf_rs_close_pending();
    if (!perf_rs_api()) return;
    g_perf_rs_units_known=0; /* Setup in this fixture uses unwrapped real GL. */
    g_perf_rs_state_cache_on=1;
    g_current_program=program;
    g_current_program_valid=1;
    GLint active=0;
    g_perf_rs_gl.get(0x84E0,&active);
    g_active_texture_unit=(GLenum)active;
    for (GLuint i=0; i<64; i++) {
        GLint buffer=0; GLint64 offset=0, length=0;
        g_perf_rs_gl.get_i(0x8A28,i,&buffer);
        g_perf_rs_gl.get_i64(0x8A29,i,&offset);
        g_perf_rs_gl.get_i64(0x8A2A,i,&length);
        trace_ubo_bind(i,(GLuint)buffer,(GLintptr)offset,(GLsizeiptr)length);
    }
}
__declspec(dllexport) void TestRegularDisableCache(void)
{ perf_rs_close_pending(); g_perf_rs_state_cache_on=0; g_current_program_valid=0; }

__declspec(dllexport) PROC TestRegularBindingProc(const char *name)
{
    if (!strcmp(name,"glBindTexture")) return (PROC)wrap_glBindTexture;
    if (!strcmp(name,"glActiveTexture")) return (PROC)wrap_glActiveTexture;
    if (!strcmp(name,"glBindTextures")) return (PROC)wrap_glBindTextures;
    if (!strcmp(name,"glBindTextureUnit")) return (PROC)wrap_glBindTextureUnit;
    if (!strcmp(name,"glBindMultiTextureEXT")) return (PROC)wrap_glBindMultiTextureEXT;
    return perf_rs_wrapper(name);
}
__declspec(dllexport) GLuint TestRegularBuild(GLuint program)
{
    if (program>=65536) return 0;
    perf_rs_build(program);
    return g_perf_rs_programs[program] ? g_perf_rs_programs[program]->program : 0;
}
__declspec(dllexport) void TestRegularHandle(GLuint64 handle, GLuint texture, GLuint sampler)
{
    bindless_texture_probe_record_handle("test",handle,texture,sampler);
    bindless_handle_rec *h=bindless_handle_find(handle);
    if (h) { h->resident_known=1; h->resident_state=1; }
}
__declspec(dllexport) void TestRegularShadow(GLuint buffer, GLsizeiptr size, const void *data)
{
    trace_buf_size(buffer,size); trace_buf_write(buffer);
    perf_cpu_shadow_replace(buffer,size,data);
    perf_rs_upload_invalidate(buffer,0,-1);
    perf_rs_upload_note(buffer,0,size,data);
}
__declspec(dllexport) void TestRegularUniform(GLuint program, GLint location, GLsizei count, const float *values)
{
    perf_rs_uniform4(program,location,count,values);
}
__declspec(dllexport) void TestRegularSampler(GLuint program, GLint location, GLint unit)
{
    perf_rs_uniform_sampler(program,location,(GLuint64)unit,0);
}
__declspec(dllexport) int TestRegularDraw(GLuint program, unsigned count)
{
    typedef void (WINAPI *draw_t)(GLenum,GLint,GLsizei);
    draw_t draw=(draw_t)perf_rs_proc("glDrawArrays");
    g_current_program=program;
    int hits=0;
    for (unsigned i=0;i<count;i++) {
        hits+=perf_rs_begin(); draw(4,0,3); perf_rs_end();
    }
    perf_rs_close_pending();
    return hits;
}
__declspec(dllexport) int TestRegularTry(GLuint program)
{
    g_current_program=program;
    int hit=perf_rs_begin(); perf_rs_end(); perf_rs_close_pending(); return hit;
}
__declspec(dllexport) int TestRegularArrayDraw(GLuint program, GLenum mode, GLint first, GLsizei count)
{
    glDrawArrays_t draw=(glDrawArrays_t)perf_rs_proc("glDrawArrays");
    g_current_program=program;
    int hit=perf_rs_begin();
    draw(mode,first,count);
    perf_rs_end();
    perf_rs_close_pending();
    return hit;
}
__declspec(dllexport) int TestRegularIndirectDraw(GLuint program, GLint draw_id, int indexed)
{
    if (!perf_rs_api()) return -1;
    g_current_program=program;
    GLint location=g_perf_rs_gl.location(program,"_amdshim_bindless_draw_id");
    bindless_drawid_set(location,draw_id);
    unsigned long long before=g_perf_rs_draws;
    perf_rs_indirect_begin(location,draw_id);
    if (indexed) {
        typedef void (WINAPI *draw_t)(GLenum,GLsizei,GLenum,const void *,GLsizei,GLint,GLuint);
        ((draw_t)perf_rs_proc("glDrawElementsInstancedBaseVertexBaseInstance"))(4,6,0x1405,NULL,1,0,0);
    } else {
        ((glDrawArrays_t)perf_rs_proc("glDrawArrays"))(4,0,6);
    }
    perf_rs_end();
    return (int)(g_perf_rs_draws-before);
}
__declspec(dllexport) int TestRegularReplay(GLuint program, int indexed, int lowered, int all_zero)
{
    if (!perf_rs_api()) return -1;
    g_current_program=program;
    g_bindless_drawid_fix_on=1;
    g_bindless_drawid_all_zero_on=all_zero;
    g_bindless_drawid_bounded_nonzero_on=g_bindless_drawid_vbc2_only_on=0;
    g_bindless_perdraw_ubo_fix_on=g_unified_ubo_state=1;
    g_perf_pointer_replay_fastpath_on=1;
    for (GLuint slot=0;slot<2;slot++) {
        GLint buffer=0; GLint64 offset=0,length=0;
        g_perf_rs_gl.get_i(0x8A28,slot,&buffer);
        g_perf_rs_gl.get_i64(0x8A29,slot,&offset);
        g_perf_rs_gl.get_i64(0x8A2A,slot,&length);
        trace_ubo_bind(slot,(GLuint)buffer,(GLintptr)offset,(GLsizeiptr)length);
        g_nv_ubo[slot].addr=make_fake_addr((GLuint)buffer,(GLuint)offset);
        g_nv_ubo[slot].len=(GLuint64)length;
    }
    GLint element=0;
    g_perf_rs_gl.get(0x8895,&element);
    g_bound_buffer[GL_ELEMENT_ARRAY_BUFFER]=(GLuint)element;
    g_bound_buffer[GL_DRAW_INDIRECT_BUFFER]=0;
    unsigned char records[96]={0};
    size_t stride=indexed?48:16;
    for (unsigned i=0;i<2;i++) {
        GLuint count=6,instances=1;
        memcpy(records+i*stride,&count,4);
        memcpy(records+i*stride+4,&instances,4);
        if (indexed) {
            GLuint64 address=make_fake_addr((GLuint)element,0),length=24;
            memcpy(records+i*stride+32,&address,8);
            memcpy(records+i*stride+40,&length,8);
        }
    }
    int enabled=g_perf_regular_samplers_on;
    g_perf_regular_samplers_on=lowered;
    unsigned long long before=g_perf_rs_draws;
    g_bindless_submission_depth++;
    int ok=indexed?bindless_draw_elements(4,0x1405,records,2,0,0)
                  :bindless_draw_arrays(4,records,2,0,0);
    g_bindless_submission_depth--;
    g_perf_regular_samplers_on=enabled;
    g_bindless_drawid_all_zero_on=0;
    return ok?(int)(g_perf_rs_draws-before):-1;
}
__declspec(dllexport) void TestRegularDropShadow(GLuint buffer)
{
    perf_cpu_shadow_invalidate(buffer); perf_rs_upload_invalidate(buffer,0,-1);
}
__declspec(dllexport) void TestRegularAllocate(GLuint buffer, GLsizeiptr size)
{
    wrap_glNamedBufferData(buffer,size,NULL,0x88E8);
}
__declspec(dllexport) void TestRegularUpload(GLuint buffer, GLintptr offset, GLsizeiptr size, const void *data)
{
    wrap_glNamedBufferSubData(buffer,offset,size,data);
}
__declspec(dllexport) int TestRegularUploadBytes(GLuint buffer, GLintptr offset, GLsizeiptr size, void *data, int legacy)
{
    return legacy ? perf_pointer_shadow_copy(buffer,offset,size,data)
                  : perf_rs_upload_copy(buffer,offset,size,data);
}
__declspec(dllexport) void TestRegularCopyBytes(GLuint source, GLuint dest, GLintptr offset, GLsizeiptr size)
{
    wrap_glCopyNamedBufferSubData(source,dest,offset,offset,size);
}
__declspec(dllexport) void TestRegularClearBytes(GLuint buffer, GLintptr offset, GLsizeiptr size)
{
    unsigned char zero=0;
    perf_emitter_clear_named_buffer_range(buffer,0x8232,offset,size,0x8D94,0x1401,&zero);
}
__declspec(dllexport) void TestRegularMapBytes(GLuint buffer)
{
    void *data=wrap_glMapNamedBufferRange(buffer,0,16,2);
    if (data) wrap_glUnmapNamedBuffer(buffer);
}
__declspec(dllexport) void *TestRegularPersistentTail(GLuint buffer, GLsizeiptr size,
    const void *data, GLintptr offset, GLsizeiptr length)
{
    wrap_glNamedBufferStorage(buffer,size,data,0xC2);
    return wrap_glMapNamedBufferRange(buffer,offset,length,0xC2);
}
__declspec(dllexport) void TestRegularUnmap(GLuint buffer) { wrap_glUnmapNamedBuffer(buffer); }
__declspec(dllexport) void TestRegularGPUBuffer(GLuint buffer)
{
    wrap_glBindBufferBase(GL_SHADER_STORAGE_BUFFER,12,buffer);
    wrap_glBindBufferBase(GL_SHADER_STORAGE_BUFFER,12,0);
}
__declspec(dllexport) void TestRegularDeleteBuffer(GLuint buffer) { wrap_glDeleteBuffers(1,&buffer); }
__declspec(dllexport) void TestRegularUpdate4(GLuint program, GLint location, GLsizei count, const float *values, int api)
{
    g_current_program=program;
    if (api==0) perf_rs_program4fv(program,location,count,values);
    else wrap_glUniform4fv(location,count,values);
}
__declspec(dllexport) void TestRegularUpdateSampler(GLuint program, GLint location, GLuint64 handle)
{
    wrap_glProgramUniformHandleui64ARB(program,location,handle);
}
__declspec(dllexport) void TestRegularUpdateUnit(GLuint program, GLint location, GLint unit)
{
    perf_rs_program1i(program,location,unit);
}
__declspec(dllexport) void TestRegularDeleteTexture(GLuint texture) { perf_rs_delete_textures(1,&texture); }
__declspec(dllexport) void TestRegularDeleteSampler(GLuint sampler) { perf_rs_delete_samplers(1,&sampler); }
__declspec(dllexport) void TestRegularRetire(GLuint program) { perf_rs_forget(program); }
__declspec(dllexport) void TestRegularContextChange(void) { perf_rs_context_change((HGLRC)(uintptr_t)1); }
__declspec(dllexport) int TestRegularSlots(GLuint program)
{
    return program<65536 && g_perf_rs_programs[program] ? g_perf_rs_programs[program]->count : 0;
}
__declspec(dllexport) int TestRegularRanges(GLuint program, GLint *ranges)
{
    perf_rs_program *p=program<65536?g_perf_rs_programs[program]:NULL;
    if (!p) return 0;
    if (ranges) memcpy(ranges,p->ranges,(size_t)p->range_count*sizeof(perf_rs_range));
    return p->range_count;
}
__declspec(dllexport) void TestRegularMetadataLayout(GLint *values)
{
    values[0]=sizeof(perf_rs_program);
    values[1]=offsetof(perf_rs_program,byte_fallbacks);
    values[2]=offsetof(perf_rs_program,range_count);
    values[3]=offsetof(perf_rs_program,ranges);
}
__declspec(dllexport) void *TestRegularProgramTable(void) { return g_perf_rs_programs; }
__declspec(dllexport) void TestRegularFailure(GLuint program, uint64_t *values)
{
    perf_rs_program *p=program<65536?g_perf_rs_programs[program]:NULL;
    if (!p || !values) return;
    values[0]=p->byte_fallbacks; values[1]=p->missing_buffer;
    values[2]=p->missing_binding; values[3]=p->missing_offset;
    values[4]=p->missing_size; values[5]=p->missing_mapped;
    values[6]=p->missing_gpu;
}
__declspec(dllexport) void TestRegularModelStarted(void) { g_perf_rs_draws=1; }
__declspec(dllexport) void TestRegularResident(GLuint64 handle, int resident)
{
    bindless_handle_rec *h=bindless_handle_find(handle);
    if (h) { h->resident_known=1; h->resident_state=(unsigned char)resident; }
}
__declspec(dllexport) int TestRegularUIKind(GLuint program)
{ return program<65536 && g_perf_rs_programs[program] ? g_perf_rs_programs[program]->ui_textures : 0; }
__declspec(dllexport) void TestRegularScalar4(GLuint program, GLint location, const float *value, int direct)
{
    g_current_program=program;
    if (direct) perf_rs_program4f(program,location,value[0],value[1],value[2],value[3]);
    else wrap_glUniform4f(location,value[0],value[1],value[2],value[3]);
}
__declspec(dllexport) int TestRegularMulti(GLuint program)
{
    const GLsizei counts[2]={3,3};
    const void *offsets[2]={NULL,NULL};
    const GLint bases[2]={0,0};
    g_current_program=program;
    unsigned long long before=g_perf_rs_ui_draws;
    wrap_glMultiDrawElementsBaseVertex(4,counts,0x1405,offsets,2,bases);
    return (int)(g_perf_rs_ui_draws-before);
}
__declspec(dllexport) int TestRegularIndexed(GLuint program)
{
    g_current_program=program;
    unsigned long long before=g_perf_rs_draws;
    wrap_glDrawElementsBaseVertex(4,3,0x1405,NULL,0);
    return (int)(g_perf_rs_draws-before);
}

static __typeof__(g_perf_rs_gl) test_rs_gl;
static uint64_t test_rs_calls[10];
#define TEST_RS_CALL(name, index, params, args) \
    static void WINAPI test_rs_##name params { test_rs_calls[index]++; test_rs_gl.name args; }
TEST_RS_CALL(get,0,(GLenum e,GLint *p),(e,p))
TEST_RS_CALL(get_i,1,(GLenum e,GLuint i,GLint *p),(e,i,p))
TEST_RS_CALL(get_i64,2,(GLenum e,GLuint i,GLint64 *p),(e,i,p))
TEST_RS_CALL(get_tex,3,(GLuint t,GLenum e,GLint *p),(t,e,p))
TEST_RS_CALL(active,4,(GLenum e),(e))
TEST_RS_CALL(bind_tex,5,(GLenum e,GLuint t),(e,t))
TEST_RS_CALL(bind_sampler,6,(GLuint u,GLuint s),(u,s))
TEST_RS_CALL(use,7,(GLuint p),(p))
TEST_RS_CALL(u1,8,(GLuint p,GLint l,GLint v),(p,l,v))
TEST_RS_CALL(bind_multi_tex,9,(GLenum u,GLenum t,GLuint n),(u,t,n))
#undef TEST_RS_CALL
__declspec(dllexport) int TestRegularMeasure(GLuint program, unsigned count, uint64_t *calls)
{
    if (!perf_rs_api()) return -1;
    test_rs_gl=g_perf_rs_gl;
    memset(test_rs_calls,0,sizeof test_rs_calls);
#define COUNT_RS(name) g_perf_rs_gl.name=test_rs_##name
    COUNT_RS(get); COUNT_RS(get_i); COUNT_RS(get_i64); COUNT_RS(get_tex);
    COUNT_RS(active); COUNT_RS(bind_tex); COUNT_RS(bind_sampler); COUNT_RS(use); COUNT_RS(u1);
    if (g_perf_rs_gl.bind_multi_tex) { COUNT_RS(bind_multi_tex); }
#undef COUNT_RS
    int hits=TestRegularDraw(program,count);
    memcpy(calls,test_rs_calls,sizeof test_rs_calls);
    g_perf_rs_gl=test_rs_gl;
    return hits;
}
__declspec(dllexport) int TestRegularTargetBind(int enabled)
{
    if (!perf_rs_api()) return 0;
    g_perf_rs_gl.bind_multi_tex=enabled
        ? (__typeof__(g_perf_rs_gl.bind_multi_tex))perf_rs_proc("glBindMultiTextureEXT") : NULL;
    return g_perf_rs_gl.bind_multi_tex!=NULL;
}

__declspec(dllexport) void TestRegularBatchProgram(int enabled)
{
    g_perf_rs_batch_program_on=enabled;
}

/* Exercise actual draws and sampler scopes in one current AMD context. */
__declspec(dllexport) double TestRegularBatchMeasure(GLuint program, int enabled,
    int alternate, unsigned batch_size, unsigned batches, uint64_t *calls)
{
    if (!perf_rs_api() || !batch_size || batch_size>512) return -1;
    typedef void (WINAPI *finish_t)(void);
    finish_t finish=(finish_t)perf_rs_proc("glFinish");
    test_rs_gl=g_perf_rs_gl;
    memset(test_rs_calls,0,sizeof test_rs_calls);
#define COUNT_RS(name) g_perf_rs_gl.name=test_rs_##name
    COUNT_RS(get); COUNT_RS(get_i); COUNT_RS(get_i64); COUNT_RS(get_tex);
    COUNT_RS(active); COUNT_RS(bind_tex); COUNT_RS(bind_sampler); COUNT_RS(use); COUNT_RS(u1);
    if (g_perf_rs_gl.bind_multi_tex) { COUNT_RS(bind_multi_tex); }
#undef COUNT_RS
    int saved=g_perf_rs_batch_on;
    g_perf_rs_batch_on=enabled;
    g_current_program=program;
    GLint location=g_perf_rs_gl.location(program,"_amdshim_bindless_draw_id");
    typedef void (WINAPI *draw_t)(GLenum,GLsizei,GLenum,const void *,GLsizei,GLint,GLuint);
    draw_t draw=(draw_t)perf_rs_proc("glDrawElementsInstancedBaseVertexBaseInstance");
    uint64_t before=g_perf_rs_draws;
    LARGE_INTEGER start,end,hz;
    QueryPerformanceFrequency(&hz);
    finish(); QueryPerformanceCounter(&start);
    for (unsigned b=0;b<batches;b++) {
        perf_rs_indirect_batch_begin((GLsizei)batch_size);
        for (unsigned i=0;i<batch_size;i++) {
            GLint draw_id=alternate?(GLint)(i&1):0;
            bindless_drawid_set(location,draw_id);
            perf_rs_indirect_begin(location,draw_id);
            draw(4,6,0x1405,NULL,1,0,0);
            perf_rs_end();
        }
        perf_rs_indirect_batch_end();
        bindless_drawid_set(location,0);
    }
    finish(); QueryPerformanceCounter(&end);
    memcpy(calls,test_rs_calls,sizeof test_rs_calls);
    calls[10]=g_perf_rs_draws-before;
    g_perf_rs_gl=test_rs_gl;
    g_perf_rs_batch_on=saved;
    return 1000.0*(end.QuadPart-start.QuadPart)/hz.QuadPart;
}
__declspec(dllexport) int TestRegularUnits(GLuint program, GLint *units)
{
    perf_rs_program *p=program<65536?g_perf_rs_programs[program]:NULL;
    if (!p) return 0;
    for (int i=0;i<p->count;i++) {
        units[4*i]=p->samplers[i].original;
        units[4*i+1]=p->sampler_units[i];
        units[4*i+2]=(GLint)p->samplers[i].type;
        units[4*i+3]=p->samplers[i].block;
    }
    return p->count;
}
__declspec(dllexport) void TestRegularStatus(GLuint program, uint64_t *values)
{
    perf_rs_program *p=program<65536?g_perf_rs_programs[program]:NULL;
    values[0]=p!=NULL; values[1]=p?p->program:0; values[2]=p?p->count:0;
    memcpy(values+3,g_perf_rs_rejects,sizeof g_perf_rs_rejects);
}
