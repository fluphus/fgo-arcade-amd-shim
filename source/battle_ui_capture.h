/* One complete UI frame after F8. No GL setters or GPU readbacks. */
#ifndef BATTLE_UI_TRACE_DIR
#define BATTLE_UI_TRACE_DIR "D:\\fgoArcade\\captures\\battle-ui\\"
#endif
static int g_battle_ui_enabled = -1, g_battle_ui_started, g_battle_ui_finished;
static HANDLE g_battle_ui_file = INVALID_HANDLE_VALUE;
static uint64_t g_battle_ui_first_frame, g_battle_ui_first_tick, g_battle_ui_bytes;
static uint64_t g_battle_ui_poll_frame = UINT64_MAX, g_battle_ui_sequence;
static unsigned char g_battle_ui_program_seen[65536];
static GLint g_battle_ui_locations[65536][6];
static char g_battle_ui_json[24576];
static size_t g_battle_ui_json_length;
static int g_battle_ui_overflow;
static uint64_t g_battle_ui_shader_bytes;
static unsigned g_battle_ui_mapped_reads, g_battle_ui_shadow_reads;
static void (WINAPI *battle_ui_geti)(GLenum, GLint *);
static void (WINAPI *battle_ui_getii)(GLenum, GLuint, GLint *);
static void (WINAPI *battle_ui_geti64i)(GLenum, GLuint, GLint64 *);
static void (WINAPI *battle_ui_getattrib)(GLuint, GLenum, GLint *);
static GLboolean (WINAPI *battle_ui_enabled)(GLenum);
static GLint (WINAPI *battle_ui_location)(GLuint, const char *);
static void (WINAPI *battle_ui_uniform)(GLuint, GLint, float *);

static void battle_ui_append(const char *format, ...)
{
    if (g_battle_ui_overflow) return;
    size_t remaining = sizeof g_battle_ui_json - g_battle_ui_json_length;
    va_list args;
    va_start(args, format);
    int n = vsnprintf(g_battle_ui_json + g_battle_ui_json_length, remaining, format, args);
    va_end(args);
    if (n < 0 || (size_t)n >= remaining) { g_battle_ui_overflow = 1; return; }
    g_battle_ui_json_length += (size_t)n;
}

static void battle_ui_finish(void)
{
    if (g_battle_ui_file != INVALID_HANDLE_VALUE) CloseHandle(g_battle_ui_file);
    g_battle_ui_file = INVALID_HANDLE_VALUE;
    g_battle_ui_finished = 1;
}

static int battle_ui_write(void)
{
    DWORD written = 0;
    if (g_battle_ui_overflow ||
        g_battle_ui_bytes + g_battle_ui_json_length > 4u * 1024u * 1024u ||
        !WriteFile(g_battle_ui_file, g_battle_ui_json, (DWORD)g_battle_ui_json_length,
                   &written, NULL) || written != g_battle_ui_json_length) {
        battle_ui_finish(); return 0;
    }
    g_battle_ui_bytes += written;
    return 1;
}

static int battle_ui_cpu_copy(GLuint buffer, GLintptr offset, GLsizeiptr size, void *dst)
{
    if (!buffer || buffer >= (1u << 20) || offset < 0 || size <= 0) return 0;
    bindless_map_rec *m = bindless_map_find(buffer);
    if (m && m->ptr && (m->access & 2u || m->access == 0x88b9 || m->access == 0x88ba) &&
        offset >= m->offset && offset - m->offset <= m->length &&
        size <= m->length - (offset - m->offset)) {
        memcpy(dst, m->ptr + (size_t)(offset - m->offset), (size_t)size);
        g_battle_ui_mapped_reads++; return 1;
    }
    if (perf_cpu_shadow_copy(buffer, offset, size, dst)) {
        g_battle_ui_shadow_reads++; return 1;
    }
    return 0;
}

static void battle_ui_sample(GLuint buffer, GLintptr offset, GLsizeiptr size)
{
    unsigned char bytes[64];
    if (!buffer || buffer >= (1u << 20) || size <= 0 || size > sizeof bytes ||
        !battle_ui_cpu_copy(buffer, offset, size, bytes)) {
        battle_ui_append("null"); return;
    }
    battle_ui_append("\"");
    for (GLsizeiptr i = 0; i < size; i++) battle_ui_append("%02x", bytes[i]);
    battle_ui_append("\"");
}

static int battle_ui_resolve(void)
{
#define BATTLE_UI_PROC(target, name) do { \
    PROC p = trace_resolve(name); \
    if ((uintptr_t)p <= 3 || p == (PROC)(intptr_t)-1) return 0; \
    target = (void *)p; \
} while (0)
    BATTLE_UI_PROC(battle_ui_geti, "glGetIntegerv");
    BATTLE_UI_PROC(battle_ui_getii, "glGetIntegeri_v");
    BATTLE_UI_PROC(battle_ui_geti64i, "glGetInteger64i_v");
    BATTLE_UI_PROC(battle_ui_getattrib, "glGetVertexAttribiv");
    BATTLE_UI_PROC(battle_ui_enabled, "glIsEnabled");
    BATTLE_UI_PROC(battle_ui_location, "glGetUniformLocation");
    BATTLE_UI_PROC(battle_ui_uniform, "glGetUniformfv");
#undef BATTLE_UI_PROC
    return 1;
}

static void battle_ui_program(GLuint program)
{
    if (program >= 65536 || g_battle_ui_program_seen[program]) return;
    g_battle_ui_program_seen[program] = 1;
    const char *names[] = {"g_constants[0]", "g_constants[1]", "g_constants[2]",
        "g_constants[3]", "g_mask_texcoord_from_projections[0]",
        "g_mask_texcoord_from_projections[1]"};
    int constants_vec4 = 0, mask_vec4 = 0;
    for (int i = 0; i < g_prog_shader_count[program] && i < 8; i++) {
        GLuint shader = g_prog_shaders[program][i];
        if (shader < 65536 && g_shader_src[shader].src) {
            constants_vec4 |= strstr(g_shader_src[shader].src, "uniform vec4 g_constants[") != NULL;
            mask_vec4 |= strstr(g_shader_src[shader].src, "uniform vec4 g_mask_texcoord_from_projections[") != NULL;
        }
        if (shader >= 65536 || !g_shader_src[shader].src ||
            g_shader_src[shader].len <= 0 || g_shader_src[shader].len > 256*1024 ||
            g_battle_ui_shader_bytes + g_shader_src[shader].len > 1u*1024u*1024u) continue;
        char path[MAX_PATH];
        snprintf(path, sizeof path, BATTLE_UI_TRACE_DIR "battle_ui_%lu_p%u_s%u.glsl",
                 (unsigned long)GetCurrentProcessId(), program, shader);
        HANDLE file = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_DELETE,
                                  NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        if (file != INVALID_HANDLE_VALUE) {
            DWORD written = 0;
            WriteFile(file, g_shader_src[shader].src, (DWORD)g_shader_src[shader].len, &written, NULL);
            g_battle_ui_shader_bytes += written;
            CloseHandle(file);
        }
    }
    /* Unknown uniforms can be matrices, which do not fit the vec4 buffer. */
    for (unsigned i = 0; i < 6; i++)
        g_battle_ui_locations[program][i] = (i < 4 ? constants_vec4 : mask_vec4)
            ? battle_ui_location(program, names[i]) : -1;
}

static void battle_ui_trace_multi(GLenum mode, const GLsizei *count, GLenum type,
    const void *const *indices, GLsizei drawcount, const GLint *basevertex)
{
    DWORD saved_error = GetLastError();
    if (g_battle_ui_enabled < 0)
        g_battle_ui_enabled = GetFileAttributesA(BATTLE_UI_TRACE_DIR "battle_ui_trace_v1.on") != INVALID_FILE_ATTRIBUTES;
    if (!g_battle_ui_enabled || g_battle_ui_finished) goto out;
    uint64_t now = GetTickCount64();
    if (!g_battle_ui_started) {
        if (g_battle_ui_poll_frame == g_frame_count) goto out;
        g_battle_ui_poll_frame = g_frame_count;
        if (!(GetAsyncKeyState(VK_F8) & 0x8000)) goto out;
        g_battle_ui_started = 1;
        g_battle_ui_first_frame = g_frame_count + 1;
        g_battle_ui_first_tick = now;
        if (!battle_ui_resolve()) { battle_ui_finish(); goto out; }
        char path[MAX_PATH];
        snprintf(path, sizeof path, BATTLE_UI_TRACE_DIR "battle_ui_%lu.jsonl", (unsigned long)GetCurrentProcessId());
        g_battle_ui_file = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_DELETE,
            NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        if (g_battle_ui_file == INVALID_HANDLE_VALUE) { battle_ui_finish(); goto out; }
    }
    if (now - g_battle_ui_first_tick >= 5000 ||
        g_frame_count > g_battle_ui_first_frame) { battle_ui_finish(); goto out; }
    if (g_frame_count < g_battle_ui_first_frame ||
        !count || !indices || drawcount <= 0 || g_current_program >= 65536) goto out;
    battle_ui_program(g_current_program);
    g_battle_ui_json_length = 0; g_battle_ui_overflow = 0;
    g_battle_ui_mapped_reads = g_battle_ui_shadow_reads = 0;
    GLint fb = -1, vao = -1, ebo = -1, viewport[4] = {0}, scissor[4] = {0};
    battle_ui_geti(0x8ca6, &fb); battle_ui_geti(0x85b5, &vao); battle_ui_geti(0x8895, &ebo);
    battle_ui_geti(0x0ba2, viewport); battle_ui_geti(0x0c10, scissor);
    battle_ui_append("{\"seq\":%llu,\"frame\":%llu,\"tick\":%llu,\"program\":%u,\"fbo\":%d,\"vao\":%d,\"ebo\":%d,\"mode\":%u,\"type\":%u,\"drawcount\":%d,",
        (unsigned long long)++g_battle_ui_sequence, (unsigned long long)g_frame_count,
        (unsigned long long)now, g_current_program, fb, vao, ebo, mode, type, drawcount);
    battle_ui_append("\"viewport\":[%d,%d,%d,%d],\"scissor\":[%d,%d,%d,%d],\"caps\":[",
        viewport[0],viewport[1],viewport[2],viewport[3],scissor[0],scissor[1],scissor[2],scissor[3]);
    const GLenum caps[] = {0x0be2,0x0c11,0x0b71,0x0b90,0x8f9d,0x8d69,0x8db9};
    for (unsigned i=0;i<sizeof caps/sizeof caps[0];i++)
        battle_ui_append("%s[%u,%u]", i?",":"", caps[i], (unsigned)battle_ui_enabled(caps[i]));
    battle_ui_append("],\"state\":[");
    const GLenum states[] = {0x80c9,0x80c8,0x80cb,0x80ca,0x8009,0x883d,0x0b74,0x0b72,0x0b92,0x0b97,0x0b93,0x0b98,0x0b94,0x0b95,0x0b96,0x0c01,0x8f9e};
    for(unsigned i=0;i<sizeof states/sizeof states[0];i++) {
        GLint value = -1; battle_ui_geti(states[i], &value);
        battle_ui_append("%s[%u,%d]", i?",":"", states[i], value);
    }
    GLint color_mask[4]={0}; battle_ui_geti(0x0c23,color_mask);
    battle_ui_append("],\"color_mask\":[%d,%d,%d,%d],\"textures_mirrored\":[%u,%u,%u,%u],\"uniforms\":[",
        color_mask[0],color_mask[1],color_mask[2],color_mask[3],g_tex_bind[0],g_tex_bind[1],g_tex_bind[2],g_tex_bind[3]);
    for(unsigned i=0;i<6;i++) {
        GLint loc=g_battle_ui_locations[g_current_program][i]; uint32_t bits[4]={0}; float v[4]={0};
        if(loc>=0) { battle_ui_uniform(g_current_program,loc,v); memcpy(bits,v,sizeof bits); }
        battle_ui_append("%s[%d,%u,%u,%u,%u]",i?",":"",loc,bits[0],bits[1],bits[2],bits[3]);
    }
    GLint ub=0; GLint64 uo=0,ul=0;
    battle_ui_getii(0x8a28,0,&ub); battle_ui_geti64i(0x8a29,0,&uo); battle_ui_geti64i(0x8a2a,0,&ul);
    battle_ui_append("],\"ubo0\":[%d,%lld,%lld,",ub,(long long)uo,(long long)ul);
    battle_ui_sample((GLuint)ub,uo,64); battle_ui_append("],\"commands\":[");
    uint32_t sample_index[3]={0}; int have_indices=0;
    unsigned index_size=type==0x1401?1:type==0x1403?2:type==0x1405?4:0;
    for(int i=0;i<drawcount && i<32;i++) {
        uint64_t offset=(uintptr_t)indices[i];
        GLint base=basevertex?basevertex[i]:0;
        battle_ui_append("%s[%d,%llu,%d]",i?",":"",count[i],(unsigned long long)offset,base);
        if(!i && index_size && count[i]>=3 && offset<=INT64_MAX && ebo>0 && ebo<(1u<<20)) {
            unsigned char raw[12]={0};
            if(battle_ui_cpu_copy((GLuint)ebo,(GLintptr)offset,index_size*3,raw)) {
                have_indices=1;
                for(unsigned j=0;j<3;j++) memcpy(&sample_index[j],raw+j*index_size,index_size);
            }
        }
    }
    battle_ui_append("],\"indices_available\":%d,\"indices\":[%u,%u,%u],\"attributes\":[",
        have_indices,sample_index[0],sample_index[1],sample_index[2]);
    for(unsigned a=0;a<8;a++) {
        GLint en=0,sz=0,ty=0,norm=0,integer=0,rel=0,binding=-1,buf=0,stride=0,divisor=0;
        GLint64 offset=0;
        battle_ui_getattrib(a,0x8622,&en); battle_ui_getattrib(a,0x8623,&sz);
        battle_ui_getattrib(a,0x8625,&ty); battle_ui_getattrib(a,0x886a,&norm);
        battle_ui_getattrib(a,0x88fd,&integer); battle_ui_getattrib(a,0x82d5,&rel);
        battle_ui_getattrib(a,0x82d4,&binding);
        if(binding>=0 && binding<16) {
            battle_ui_getii(0x8f4f,binding,&buf); battle_ui_getii(0x82d8,binding,&stride);
            battle_ui_getii(0x82d6,binding,&divisor); battle_ui_geti64i(0x82d7,binding,&offset);
        }
        battle_ui_append("%s{\"a\":%u,\"live\":[%d,%d,%d,%d,%d,%d,%d,%d,%lld,%d,%d],\"requested\":[%d,%u,%u,%u,%u,%u],\"nv_keep\":[%llu,%lld],\"samples\":[",
            a?",":"",a,en,sz,ty,norm,integer,rel,binding,buf,(long long)offset,stride,divisor,
            g_abind[a].set? (int)g_abind[a].binding:-1,g_fmt[a].relativeoffset,
            (unsigned)g_fmt[a].size,g_fmt[a].type,(unsigned)g_fmt[a].normalized,(unsigned)g_fmt[a].is_int,
            (unsigned long long)g_nv_va_keep[a].addr,(long long)g_nv_va_keep[a].len);
        for(unsigned j=0;j<3;j++) {
            if(j) battle_ui_append(",");
            int64_t vertex=(int64_t)sample_index[j]+(basevertex?basevertex[0]:0);
            if(!have_indices || !en || divisor || vertex<0 || stride<0 || offset<0 || rel<0 ||
               vertex>(INT64_MAX-offset-rel)/(stride?stride:1)) battle_ui_append("null");
            else battle_ui_sample((GLuint)buf,offset+rel+vertex*stride,16);
        }
        GLuint requested_binding = g_abind[a].set ? g_abind[a].binding : a;
        battle_ui_append("],\"nv_binding\":[");
        if (requested_binding < NV_MAX_ATTRIBS) {
            battle_ui_append("%u,%llu,%lld,%u,%lld,%d", requested_binding,
                (unsigned long long)g_nv_va[requested_binding].addr,
                (long long)g_nv_va[requested_binding].len,
                g_vbuf[requested_binding].buffer,
                (long long)g_vbuf[requested_binding].offset,
                g_vbuf[requested_binding].stride);
        }
        battle_ui_append("]}");
    }
    battle_ui_append("],\"cpu_reads\":{\"mapped\":%u,\"shadow\":%u}}\n",
                    g_battle_ui_mapped_reads,g_battle_ui_shadow_reads); battle_ui_write();
out:
    SetLastError(saved_error);
}
