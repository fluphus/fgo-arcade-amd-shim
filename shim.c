#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>

typedef PROC (WINAPI *wglGetProcAddress_t)(LPCSTR);
typedef HGLRC (WINAPI *wglCreateContext_t)(HDC);
typedef BOOL (WINAPI *wglMakeCurrent_t)(HDC, HGLRC);
typedef BOOL (WINAPI *wglDeleteContext_t)(HGLRC);

static HMODULE g_real;
static int g_log_on;
static wglGetProcAddress_t real_wglGetProcAddress;
static wglCreateContext_t real_wglCreateContext;
static wglMakeCurrent_t real_wglMakeCurrent;
static wglDeleteContext_t real_wglDeleteContext;

typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef unsigned int GLbitfield;
typedef unsigned char GLboolean;
typedef int GLint;
typedef unsigned long long GLuint64;
typedef int GLsizei;
typedef void *GLvoid;
typedef long long GLintptr;
typedef long long GLsizeiptr;

typedef void (WINAPI *glBindVertexBuffer_t)(GLuint, GLuint, GLintptr, GLsizei);
typedef void (WINAPI *glBindBuffer_t)(GLenum, GLuint);
static glBindVertexBuffer_t real_glBindVertexBuffer;
static glBindBuffer_t real_glBindBuffer;
static GLuint g_bound_buffer[65536];
static void ensure_gl_bind_vertex_buffer(void);
static void ensure_gl_bind_buffer(void);
static uint64_t make_fake_addr(GLuint buffer, GLuint offset);
static void decode_fake_addr(uint64_t addr, GLuint *buffer, GLuint *offset);
static int is_fake_addr(uint64_t addr);

static void glog(const char *fmt, ...);
static void maybe_capture_frame(void);
static PROC trace_resolve(const char *name);

static const char *nv_to_arb(const char *name)
{
    static const struct { const char *nv; const char *arb; } map[] = {
        {"glGetTextureHandleNV", "glGetTextureHandleARB"},
        {"glGetTextureSamplerHandleNV", "glGetTextureSamplerHandleARB"},
        {"glMakeTextureHandleResidentNV", "glMakeTextureHandleResidentARB"},
        {"glMakeTextureHandleNonResidentNV", "glMakeTextureHandleNonResidentARB"},
        {"glGetImageHandleNV", "glGetImageHandleARB"},
        {"glMakeImageHandleResidentNV", "glMakeImageHandleResidentARB"},
        {"glMakeImageHandleNonResidentNV", "glMakeImageHandleNonResidentARB"},
        {"glUniformHandleui64NV", "glUniformHandleui64ARB"},
        {"glUniformHandleui64vNV", "glUniformHandleui64vARB"},
        {"glProgramUniformHandleui64NV", "glProgramUniformHandleui64ARB"},
        {"glProgramUniformHandleui64vNV", "glProgramUniformHandleui64vARB"},
        {"glIsTextureHandleResidentNV", "glIsTextureHandleResidentARB"},
        {"glIsImageHandleResidentNV", "glIsImageHandleResidentARB"},
        {"glVertexAttribL1ui64NV", "glVertexAttribL1ui64ARB"},
        {"glVertexAttribL1ui64vNV", "glVertexAttribL1ui64vARB"},
        {"glGetVertexAttribLui64vNV", "glGetVertexAttribLui64vARB"},
    };
    size_t i;
    for (i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
        if (strcmp(name, map[i].nv) == 0) return map[i].arb;
    }
    return NULL;
}

static GLboolean WINAPI stub_glMakeNamedBufferResidentNV(GLuint buffer)
{
    (void)buffer;
    return 1;
}

static void WINAPI stub_glMakeNamedBufferNonResidentNV(GLuint buffer)
{
    (void)buffer;
}

/* GL_NV_vertex_buffer_unified_memory / GL_NV_shader_buffer_load constants */
#define GL_BUFFER_GPU_ADDRESS_NV            0x8F1D
#define GL_VERTEX_ATTRIB_ARRAY_UNIFIED_NV   0x8F1E
#define GL_ELEMENT_ARRAY_UNIFIED_NV         0x8F1F
#define GL_VERTEX_ATTRIB_ARRAY_ADDRESS_NV   0x8F20
#define GL_VERTEX_ATTRIB_ARRAY_LENGTH_NV    0x8F21
#define GL_ELEMENT_ARRAY_ADDRESS_NV         0x8F29
#define GL_ELEMENT_ARRAY_LENGTH_NV          0x8F33
#define GL_GPU_ADDRESS_NV                   0x8F34
#define GL_UNIFORM_BUFFER_UNIFIED_NV        0x936E
#define GL_UNIFORM_BUFFER_ADDRESS_NV        0x936F
#define GL_UNIFORM_BUFFER_LENGTH_NV         0x9370

typedef struct {
    GLuint64 addr;
    GLsizei len;
} nv_range;

typedef struct {
    int set;
    GLint size;
    GLenum type;
    GLboolean normalized;
    GLsizei stride;
    GLuint relativeoffset;
    int is_int;
} attrib_fmt;

typedef struct {
    int set;
    GLuint buffer;
    GLintptr offset;
    GLsizei stride;
} vbuf_binding;

typedef struct {
    int set;
    GLuint binding;
} attrib_binding;

enum { NV_MAX_ATTRIBS = 16, NV_MAX_UBO = 32 };

#define MAX_PTR_MEMBERS 8
typedef struct {
    int ubo_binding;
    int ssbo_binding;
    char name[64];
    char type[64];
} ptr_member;
static struct {
    int count;
    ptr_member m[MAX_PTR_MEMBERS];
    int needs_vbo_ssbo;
    int needs_light_ssbo;
    int needs_raw_ssbo;
    int needs_emitter_count;
    int emitter_ubo_binding;
    int emitter_struct_count;
    int emitter_struct_ubo[2];
    int emitter_struct_idx[2];
    int emitter_struct_ssbo[2];
} g_shader_ptrs[65536];
static struct {
    int count;
    ptr_member m[MAX_PTR_MEMBERS];
    int needs_vbo_ssbo;
    int needs_light_ssbo;
    int needs_raw_ssbo;
    int needs_emitter_count;
    int emitter_ubo_binding;
    int emitter_struct_count;
    int emitter_struct_ubo[2];
    int emitter_struct_idx[2];
    int emitter_struct_ssbo[2];
} g_prog_ptrs[65536];

static nv_range g_nv_va[NV_MAX_ATTRIBS];
static nv_range g_nv_va_keep[NV_MAX_ATTRIBS];
static nv_range g_nv_ea;
static GLuint g_nv_ea_buf;
static GLintptr g_nv_ea_off;
static int g_nv_ea_valid;
static nv_range g_nv_ubo[NV_MAX_UBO];
static attrib_fmt g_fmt[NV_MAX_ATTRIBS];
static vbuf_binding g_vbuf[NV_MAX_ATTRIBS];
static attrib_binding g_abind[NV_MAX_ATTRIBS];
static GLuint g_enabled_mask;
static GLuint g_emitted_mask;
static int g_unified_attrib_state;
static int g_unified_elem_state;
static int g_unified_ubo_state;
static GLuint g_current_program;
static GLuint g_vbo_ssbo_buffer;
static GLuint g_vbo_ssbo_offset;
static int g_vbo_ssbo_valid;
static GLuint g_light_ssbo_buffer;
static GLintptr g_light_ssbo_offset;
static GLsizeiptr g_light_ssbo_len;
static int g_light_ssbo_valid;
static void apply_pointer_bindings(void);
static unsigned long long g_frame_count;
static int g_capture_enabled;
static int g_bluefind_on;
static int g_bluefind_logged;
static int g_blue_saw_swap;
static int g_tex563_dumped;
static GLuint g_tex_unit1;
static GLuint g_tex563_u0;
static GLuint g_tex563_u1;
static GLenum g_active_texture_unit;
static int g_tex563_record_logged;
static void bluefind_frame(void);
static GLuint g_bound_draw_framebuffer;
static GLuint g_bound_read_framebuffer;
static GLuint g_current_vao;
static GLuint g_fbo_attach[65536][8];
typedef void (WINAPI *glFramebufferTexture2D_t)(GLenum, GLenum, GLenum, GLuint, GLint);
typedef void (WINAPI *glTextureStorage2D_t)(GLuint, GLsizei, GLenum, GLsizei, GLsizei);
typedef void (WINAPI *glTextureSubImage2D_t)(GLuint, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void *);
typedef void (WINAPI *glCompressedTextureSubImage2D_t)(GLuint, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLsizei, const void *);
typedef void (WINAPI *glBindImageTexture_t)(GLuint, GLuint, GLint, GLboolean, GLint, GLenum, GLenum);
typedef void (WINAPI *glTexStorage2D_t)(GLenum, GLsizei, GLenum, GLsizei, GLsizei);
typedef void (WINAPI *glTexSubImage2D_t)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void *);
typedef void (WINAPI *glCopyImageSubData_t)(GLuint, GLenum, GLint, GLint, GLint, GLint, GLuint, GLenum, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei);
typedef void (WINAPI *glCopyTextureSubImage2D_t)(GLuint, GLint, GLint, GLint, GLint, GLint, GLint, GLsizei, GLsizei);
typedef void (WINAPI *glNamedFramebufferTexture_t)(GLuint, GLenum, GLuint, GLint);
static int g_texup_logged;
typedef GLenum (WINAPI *glGetError_t)(void);
static glGetError_t real_glGetError;
static int g_glerror_logged;

static GLenum WINAPI wrap_glGetError(void)
{
    if (!real_glGetError) real_glGetError = (glGetError_t)trace_resolve("glGetError");
    GLenum e = real_glGetError ? real_glGetError() : 0;
    if (e != 0 && g_glerror_logged < 200) {
        g_glerror_logged++;
        FILE *f = fopen("C:\\fgo\\_tools\\glshim\\keytex.log", "a");
        if (f) {
            fprintf(f, "[%llu] glGetError -> 0x%x (prog=%u)\n",
                    (unsigned long long)g_frame_count, e, g_current_program);
            fclose(f);
        }
    }
    return e;
}

static int is_key_tex(unsigned int v)
{
    switch (v) {
    case 20: case 91: case 92: case 95: case 96: case 98: case 99:
    case 101: case 104: case 106: case 107: case 109: case 110: case 111:
    case 118: case 120:
        return 1;
    default:
        return 0;
    }
}

static unsigned long long g_keytex_lines;
static void keytex_log(const char *fmt, ...)
{
    if (!g_log_on) return;
    if (g_keytex_lines >= 200000) return;
    g_keytex_lines++;
    va_list ap;
    char buf[512];
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    FILE *f = fopen("C:\\fgo\\_tools\\glshim\\keytex.log", "a");
    if (f) {
        fprintf(f, "[%llu] %s\n", (unsigned long long)g_frame_count, buf);
        fclose(f);
    }
}

static unsigned long long g_state_log_lines;
static void state_log(const char *fmt, ...)
{
    if (!g_log_on) return;
    if (g_state_log_lines >= 40000) return;
    g_state_log_lines++;
    va_list ap;
    char buf[512];
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    FILE *f = fopen("C:\\fgo\\_tools\\glshim\\swap.log", "a");
    if (f) {
        fprintf(f, "[%llu] STATE %s\n", (unsigned long long)g_frame_count, buf);
        fclose(f);
    }
}

static void texup_log(const char *fmt, ...)
{
    if (g_texup_logged >= 600) return;
    g_texup_logged++;
    va_list ap;
    char buf[512];
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    FILE *f = fopen("C:\\fgo\\_tools\\glshim\\swap.log", "a");
    if (f) {
        fprintf(f, "[%llu] TEXUP %s\n", (unsigned long long)g_frame_count, buf);
        fclose(f);
    }
}

static void WINAPI wrap_glTextureStorage2D(GLuint tex, GLsizei levels, GLenum format,
                                           GLsizei w, GLsizei h)
{
    static glTextureStorage2D_t real;
    if (!real) real = (glTextureStorage2D_t)trace_resolve("glTextureStorage2D");
    texup_log("glTextureStorage2D tex=%u levels=%d fmt=0x%x %dx%d", tex, levels, format, w, h);
    if (is_key_tex(tex)) keytex_log("glTextureStorage2D tex=%u levels=%d fmt=0x%x %dx%d", tex, levels, format, w, h);
    if (real) real(tex, levels, format, w, h);
}

static void WINAPI wrap_glTextureSubImage2D(GLuint tex, GLint level, GLint x, GLint y,
                                            GLsizei w, GLsizei h, GLenum format,
                                            GLenum type, const void *data)
{
    static glTextureSubImage2D_t real;
    if (!real) real = (glTextureSubImage2D_t)trace_resolve("glTextureSubImage2D");
    texup_log("glTextureSubImage2D tex=%u lvl=%d %d,%d %dx%d fmt=0x%x type=0x%x data=%p first=%08x%08x",
              tex, level, x, y, w, h, format, type, data,
              data ? *(const unsigned int *)data : 0,
              data ? *((const unsigned int *)data + 1) : 0);
    if (is_key_tex(tex))
        keytex_log("glTextureSubImage2D tex=%u lvl=%d %d,%d %dx%d fmt=0x%x type=0x%x data=%p",
                   tex, level, x, y, w, h, format, type, data);
    if (real) real(tex, level, x, y, w, h, format, type, data);
}

static void WINAPI wrap_glCompressedTextureSubImage2D(GLuint tex, GLint level, GLint x, GLint y,
                                                      GLsizei w, GLsizei h, GLenum format,
                                                      GLsizei size, const void *data)
{
    static glCompressedTextureSubImage2D_t real;
    if (!real) real = (glCompressedTextureSubImage2D_t)trace_resolve("glCompressedTextureSubImage2D");
    texup_log("glCompressedTextureSubImage2D tex=%u lvl=%d %d,%d %dx%d fmt=0x%x size=%d data=%p first=%08x%08x",
              tex, level, x, y, w, h, format, size, data,
              data ? *(const unsigned int *)data : 0,
              data ? *((const unsigned int *)data + 1) : 0);
    if (is_key_tex(tex))
        keytex_log("glCompressedTextureSubImage2D tex=%u lvl=%d %d,%d %dx%d fmt=0x%x size=%d data=%p",
                   tex, level, x, y, w, h, format, size, data);
    if (real) real(tex, level, x, y, w, h, format, size, data);
}

static void WINAPI wrap_glBindImageTexture(GLuint unit, GLuint tex, GLint level,
                                           GLboolean layered, GLint layer,
                                           GLenum access, GLenum format)
{
    static glBindImageTexture_t real;
    if (!real) real = (glBindImageTexture_t)trace_resolve("glBindImageTexture");
    texup_log("glBindImageTexture unit=%u tex=%u lvl=%d layer=%d access=0x%x fmt=0x%x",
              unit, tex, level, layer, access, format);
    if (is_key_tex(tex))
        keytex_log("glBindImageTexture unit=%u tex=%u lvl=%d layer=%d access=0x%x fmt=0x%x",
                   unit, tex, level, layer, access, format);
    if (real) real(unit, tex, level, layered, layer, access, format);
}

static void WINAPI wrap_glTexStorage2D(GLenum target, GLsizei levels, GLenum format,
                                       GLsizei w, GLsizei h)
{
    static glTexStorage2D_t real;
    if (!real) real = (glTexStorage2D_t)trace_resolve("glTexStorage2D");
    texup_log("glTexStorage2D target=0x%x levels=%d fmt=0x%x %dx%d", target, levels, format, w, h);
    if (real) real(target, levels, format, w, h);
}

static void WINAPI wrap_glTexSubImage2D(GLenum target, GLint level, GLint x, GLint y,
                                        GLsizei w, GLsizei h, GLenum format,
                                        GLenum type, const void *data)
{
    static glTexSubImage2D_t real;
    if (!real) real = (glTexSubImage2D_t)trace_resolve("glTexSubImage2D");
    texup_log("glTexSubImage2D target=0x%x lvl=%d %d,%d %dx%d fmt=0x%x type=0x%x data=%p",
              target, level, x, y, w, h, format, type, data);
    if (real) real(target, level, x, y, w, h, format, type, data);
}

static void WINAPI wrap_glCopyImageSubData(GLuint srcName, GLenum srcTarget, GLint srcLevel,
                                           GLint srcX, GLint srcY, GLint srcZ,
                                           GLuint dstName, GLenum dstTarget, GLint dstLevel,
                                           GLint dstX, GLint dstY, GLint dstZ,
                                           GLsizei w, GLsizei h, GLsizei d)
{
    static glCopyImageSubData_t real;
    if (!real) real = (glCopyImageSubData_t)trace_resolve("glCopyImageSubData");
    texup_log("glCopyImageSubData src=%u(t=0x%x,l=%d) %d,%d,%d -> dst=%u(t=0x%x,l=%d) %d,%d,%d %dx%dx%d",
              srcName, srcTarget, srcLevel, srcX, srcY, srcZ,
              dstName, dstTarget, dstLevel, dstX, dstY, dstZ, w, h, d);
    if (is_key_tex(srcName) || is_key_tex(dstName))
        keytex_log("glCopyImageSubData src=%u -> dst=%u %dx%dx%d",
                   srcName, dstName, w, h, d);
    if (real) real(srcName, srcTarget, srcLevel, srcX, srcY, srcZ,
                   dstName, dstTarget, dstLevel, dstX, dstY, dstZ, w, h, d);
}

static void WINAPI wrap_glCopyTextureSubImage2D(GLuint tex, GLint level,
                                                GLint x, GLint y, GLint z,
                                                GLint sx, GLint sy, GLsizei w, GLsizei h)
{
    static glCopyTextureSubImage2D_t real;
    if (!real) real = (glCopyTextureSubImage2D_t)trace_resolve("glCopyTextureSubImage2D");
    texup_log("glCopyTextureSubImage2D dst=%u lvl=%d %d,%d,%d <- src=(%d,%d) %dx%d",
              tex, level, x, y, z, sx, sy, w, h);
    if (is_key_tex(tex))
        keytex_log("glCopyTextureSubImage2D dst=%u lvl=%d %d,%d,%d <- src=(%d,%d) %dx%d",
                   tex, level, x, y, z, sx, sy, w, h);
    if (real) real(tex, level, x, y, z, sx, sy, w, h);
}

static void WINAPI wrap_glNamedFramebufferTexture(GLuint fb, GLenum attachment,
                                                  GLuint tex, GLint level)
{
    static glNamedFramebufferTexture_t real;
    if (!real) real = (glNamedFramebufferTexture_t)trace_resolve("glNamedFramebufferTexture");
    int idx = -1;
    if (attachment >= 0x8CE0 && attachment <= 0x8CE7) idx = (int)(attachment - 0x8CE0);
    if (idx >= 0 && idx < 8 && fb < 65536) g_fbo_attach[fb][idx] = tex;
    texup_log("glNamedFramebufferTexture fb=%u att=0x%x tex=%u lvl=%d", fb, attachment, tex, level);
    if (fb >= 90 || is_key_tex(tex))
        keytex_log("glNamedFramebufferTexture fb=%u att=0x%x tex=%u lvl=%d", fb, attachment, tex, level);
    if (real) real(fb, attachment, tex, level);
}

static void WINAPI wrap_glFramebufferTexture2D(GLenum target, GLenum attachment,
                                               GLenum textarget, GLuint texture, GLint level)
{
    static glFramebufferTexture2D_t real;
    if (!real) real = (glFramebufferTexture2D_t)trace_resolve("glFramebufferTexture2D");
    if (target == 0x8D40 || target == 0x8CA9 || target == 0x8CAA) {
        GLuint fb = (target == 0x8D40 || target == 0x8CA9) ? g_bound_draw_framebuffer : g_bound_read_framebuffer;
        int idx = -1;
        if (attachment >= 0x8CE0 && attachment <= 0x8CE7) idx = (int)(attachment - 0x8CE0);
        if (idx >= 0 && idx < 8 && fb < 65536) g_fbo_attach[fb][idx] = texture;
        FILE *f = fopen("C:\\fgo\\_tools\\glshim\\swap.log", "a");
        if (f) {
            fprintf(f, "[%llu] FBOATTACH fb=%u att=0x%x tex=%u\n",
                    (unsigned long long)g_frame_count, fb, attachment, texture);
            fclose(f);
        }
        if (fb >= 90 || is_key_tex(texture))
            keytex_log("glFramebufferTexture2D fb=%u att=0x%x tex=%u", fb, attachment, texture);
    }
    if (real) real(target, attachment, textarget, texture, level);
}

#define SCENE_RING_MAX 128
typedef struct {
    unsigned long long frame;
    GLuint prog;
    GLenum mode;
    GLint first;
    GLsizei count;
    GLsizei primcount;
    GLuint vao;
    GLuint tex0;
    GLuint tex1;
    GLuint ubo0, ubo1, ubo2, ubo3;
    GLuint ssbo0, ssbo1, ssbo2, ssbo3;
    GLint vp[4];
    GLuint nv_attr_mask;
} scene_rec;
static scene_rec g_scene_ring[SCENE_RING_MAX];
static int g_scene_ring_pos;
static int g_scene_ring_full;
static void record_scene_ring(const char *fn, GLenum mode, GLint first,
                              GLsizei count, GLsizei primcount);
static void dump_scene_ring(const char *why);
static void hook_gdi32_import(void);

static void WINAPI stub_glGetNamedBufferParameterui64vNV(GLuint buffer, GLenum pname, GLuint64 *params)
{
    GLuint64 v = 0;
    if (pname == GL_BUFFER_GPU_ADDRESS_NV) v = make_fake_addr(buffer, 0);
    if (params) *params = v;
    glog("NV glGetNamedBufferParameterui64vNV buffer=%u pname=0x%x -> 0x%llx\n",
         buffer, pname, (unsigned long long)v);
}

static void WINAPI stub_glGetIntegerui64vNV(GLenum pname, GLuint64 *params)
{
    GLuint64 v = 0;
    if (pname == GL_VERTEX_ATTRIB_ARRAY_ADDRESS_NV) v = g_nv_va[0].addr;
    if (pname == GL_VERTEX_ATTRIB_ARRAY_LENGTH_NV) v = (GLuint64)g_nv_va[0].len;
    if (pname == GL_ELEMENT_ARRAY_ADDRESS_NV) v = g_nv_ea.addr;
    if (pname == GL_ELEMENT_ARRAY_LENGTH_NV) v = (GLuint64)g_nv_ea.len;
    if (pname == GL_UNIFORM_BUFFER_ADDRESS_NV) v = g_nv_ubo[0].addr;
    if (pname == GL_UNIFORM_BUFFER_LENGTH_NV) v = (GLuint64)g_nv_ubo[0].len;
    if (params) *params = v;
    glog("NV glGetIntegerui64vNV pname=0x%x -> 0x%llx\n", pname, (unsigned long long)v);
}

static void WINAPI stub_glVertexAttribFormatNV(GLuint index, GLint size, GLenum type, GLboolean normalized, GLuint stride)
{
    if (index < NV_MAX_ATTRIBS) {
        g_fmt[index].set = 1;
        g_fmt[index].size = size;
        g_fmt[index].type = type;
        g_fmt[index].normalized = normalized;
        g_fmt[index].stride = (GLsizei)stride;
        g_fmt[index].is_int = 0;
    }
    glog("FMT glVertexAttribFormatNV idx=%u size=%d type=0x%x norm=%d stride=%u\n",
         index, size, type, normalized, stride);
}

static void WINAPI stub_glVertexAttribIFormatNV(GLuint index, GLint size, GLenum type, GLuint stride)
{
    if (index < NV_MAX_ATTRIBS) {
        g_fmt[index].set = 1;
        g_fmt[index].size = size;
        g_fmt[index].type = type;
        g_fmt[index].normalized = 0;
        g_fmt[index].stride = (GLsizei)stride;
        g_fmt[index].is_int = 1;
    }
    glog("FMT glVertexAttribIFormatNV idx=%u size=%d type=0x%x stride=%u\n",
         index, size, type, stride);
}

static void WINAPI stub_glVertexAttribLFormatNV(GLuint index, GLint size, GLenum type, GLuint stride)
{
    if (index < NV_MAX_ATTRIBS) {
        g_fmt[index].set = 1;
        g_fmt[index].size = size;
        g_fmt[index].type = type;
        g_fmt[index].normalized = 0;
        g_fmt[index].stride = (GLsizei)stride;
        g_fmt[index].is_int = 0;
    }
    glog("FMT glVertexAttribLFormatNV idx=%u size=%d type=0x%x stride=%u\n",
         index, size, type, stride);
}

static void WINAPI stub_glBufferAddressRangeNV(GLenum pname, GLuint index, GLuint64 address, GLsizei length)
{
    if (pname == GL_VERTEX_ATTRIB_ARRAY_ADDRESS_NV) {
        if (index < NV_MAX_ATTRIBS) {
            g_nv_va[index].addr = address; g_nv_va[index].len = length;
            if (address != 0) { g_nv_va_keep[index].addr = address; g_nv_va_keep[index].len = length; }
        }
    } else if (pname == GL_ELEMENT_ARRAY_ADDRESS_NV) {
        g_nv_ea.addr = address; g_nv_ea.len = length;
    } else if (pname == GL_UNIFORM_BUFFER_ADDRESS_NV) {
        if (index < NV_MAX_UBO) { g_nv_ubo[index].addr = address; g_nv_ubo[index].len = length; }
    }
    GLuint buf = 0; GLuint off = 0;
    if (address != 0) decode_fake_addr(address, &buf, &off);
    glog("NV glBufferAddressRangeNV pname=0x%x index=%u addr=0x%llx len=%d\n",
         pname, index, (unsigned long long)address, length);
    if (address != 0 && buf != 0) {
        glog("NV   -> decoded buf=%u off=0x%x\n", buf, off);
    }
}

static void WINAPI stub_glGetIntegerui64i_vNV(GLenum target, GLuint index, GLuint64 *params)
{
    GLuint64 v = 0;
    if (target == GL_VERTEX_ATTRIB_ARRAY_ADDRESS_NV && index < NV_MAX_ATTRIBS) v = g_nv_va[index].addr;
    if (target == GL_VERTEX_ATTRIB_ARRAY_LENGTH_NV && index < NV_MAX_ATTRIBS) v = (GLuint64)g_nv_va[index].len;
    if (target == GL_UNIFORM_BUFFER_ADDRESS_NV && index < NV_MAX_UBO) v = g_nv_ubo[index].addr;
    if (target == GL_UNIFORM_BUFFER_LENGTH_NV && index < NV_MAX_UBO) v = (GLuint64)g_nv_ubo[index].len;
    if (params) *params = v;
    glog("NV glGetIntegerui64i_vNV target=0x%x index=%u -> 0x%llx\n",
         target, index, (unsigned long long)v);
}

static void WINAPI stub_glGetBufferParameterui64vNV(GLenum target, GLenum pname, GLuint64 *params)
{
    GLuint buf = (target < 65536) ? g_bound_buffer[target] : 0;
    GLuint64 v = 0;
    if (pname == GL_BUFFER_GPU_ADDRESS_NV) v = make_fake_addr(buf, 0);
    if (params) *params = v;
    glog("NV glGetBufferParameterui64vNV target=0x%x pname=0x%x buf=%u -> 0x%llx\n",
         target, pname, buf, (unsigned long long)v);
}

static void WINAPI stub_glMultiDrawArraysIndirectBindlessNV(GLenum mode, const GLvoid *indirect, GLsizei drawCount, GLsizei stride, GLint vertexBufferCount)
{
    glog("NV glMultiDrawArraysIndirectBindlessNV mode=0x%x indirect=%p drawCount=%d stride=%d vbc=%d\n",
         mode, indirect, drawCount, stride, vertexBufferCount);
}

static void WINAPI stub_glMultiDrawElementsIndirectBindlessNV(GLenum mode, GLenum type, const GLvoid *indirect, GLsizei drawCount, GLsizei stride, GLint vertexBufferCount)
{
    glog("NV glMultiDrawElementsIndirectBindlessNV mode=0x%x type=0x%x indirect=%p drawCount=%d stride=%d vbc=%d\n",
         mode, type, indirect, drawCount, stride, vertexBufferCount);
}

/* ---------- GL call tracing wrappers ---------- */

typedef void (WINAPI *glUseProgram_t)(GLuint);
typedef void (WINAPI *glBindVertexArray_t)(GLuint);
typedef void (WINAPI *glBindBufferBase_t)(GLenum, GLuint, GLuint);
typedef void (WINAPI *glBindBufferRange_t)(GLenum, GLuint, GLuint, GLintptr, GLsizeiptr);
typedef void (WINAPI *glBufferData_t)(GLenum, GLsizeiptr, const void *, GLenum);
typedef void (WINAPI *glBufferSubData_t)(GLenum, GLintptr, GLsizeiptr, const void *);
typedef void (WINAPI *glNamedBufferData_t)(GLuint, GLsizeiptr, const void *, GLenum);
typedef void (WINAPI *glNamedBufferSubData_t)(GLuint, GLintptr, GLsizeiptr, const void *);
typedef void (WINAPI *glBufferStorage_t)(GLenum, GLsizeiptr, const void *, GLbitfield);
typedef void (WINAPI *glNamedBufferStorage_t)(GLuint, GLsizeiptr, const void *, GLbitfield);
typedef void * (WINAPI *glMapBuffer_t)(GLenum, GLenum);
typedef void * (WINAPI *glMapBufferRange_t)(GLenum, GLintptr, GLsizeiptr, GLbitfield);
typedef void * (WINAPI *glMapNamedBuffer_t)(GLuint, GLenum);
typedef void * (WINAPI *glMapNamedBufferRange_t)(GLuint, GLintptr, GLsizeiptr, GLbitfield);
typedef void (WINAPI *glVertexAttribPointer_t)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void *);
typedef void (WINAPI *glVertexAttribIPointer_t)(GLuint, GLint, GLenum, GLsizei, const void *);
typedef void (WINAPI *glVertexAttribFormat_t)(GLuint, GLint, GLenum, GLboolean, GLuint);
typedef void (WINAPI *glVertexAttribIFormat_t)(GLuint, GLint, GLenum, GLuint);
typedef void (WINAPI *glVertexAttribBinding_t)(GLuint, GLuint);
typedef void (WINAPI *glEnableVertexAttribArray_t)(GLuint);
typedef void (WINAPI *glDisableVertexAttribArray_t)(GLuint);
typedef void (WINAPI *glVertexArrayAttribFormat_t)(GLuint, GLuint, GLint, GLenum, GLboolean, GLuint);
typedef void (WINAPI *glVertexArrayAttribIFormat_t)(GLuint, GLuint, GLint, GLenum, GLuint);
typedef void (WINAPI *glVertexArrayVertexBuffer_t)(GLuint, GLuint, GLuint, GLintptr, GLsizei);
typedef void (WINAPI *glVertexArrayAttribBinding_t)(GLuint, GLuint, GLuint);
typedef void (WINAPI *glEnableVertexArrayAttrib_t)(GLuint, GLuint);
typedef void (WINAPI *glDrawArrays_t)(GLenum, GLint, GLsizei);
typedef void (WINAPI *glDrawArraysInstanced_t)(GLenum, GLint, GLsizei, GLsizei);
typedef void (WINAPI *glDrawElements_t)(GLenum, GLsizei, GLenum, const void *);
typedef void (WINAPI *glDrawElementsInstanced_t)(GLenum, GLsizei, GLenum, const void *, GLsizei);
typedef void (WINAPI *glDrawElementsInstancedBaseVertex_t)(GLenum, GLsizei, GLenum, const void *, GLsizei, GLint);
typedef void (WINAPI *glDrawElementsBaseVertex_t)(GLenum, GLsizei, GLenum, const void *, GLint);
typedef void (WINAPI *glMultiDrawElementsBaseVertex_t)(GLenum, const GLsizei *, GLenum, const void *const *, GLsizei, const GLint *);
typedef void (WINAPI *glDrawRangeElements_t)(GLenum, GLuint, GLuint, GLsizei, GLenum, const void *);
typedef void (WINAPI *glMultiDrawArrays_t)(GLenum, const GLint *, const GLsizei *, GLsizei);
typedef void (WINAPI *glMultiDrawElements_t)(GLenum, const GLsizei *, GLenum, const void *const *, GLsizei);
typedef void (WINAPI *glDrawArraysIndirect_t)(GLenum, const void *);
typedef void (WINAPI *glDrawElementsIndirect_t)(GLenum, GLenum, const void *);
typedef void (WINAPI *glMultiDrawArraysIndirect_t)(GLenum, const void *, GLsizei, GLsizei);
typedef void (WINAPI *glMultiDrawElementsIndirect_t)(GLenum, GLenum, const void *, GLsizei, GLsizei);
typedef void (WINAPI *glMultiDrawArraysIndirectCount_t)(GLenum, const void *, GLintptr, GLsizei, GLsizei);
typedef void (WINAPI *glMultiDrawElementsIndirectCount_t)(GLenum, GLenum, const void *, GLintptr, GLsizei, GLsizei);
typedef void (WINAPI *glGenBuffers_t)(GLsizei, GLuint *);
typedef void (WINAPI *glDeleteBuffers_t)(GLsizei, const GLuint *);
typedef void (WINAPI *glGetNamedBufferSubData_t)(GLuint, GLintptr, GLsizeiptr, void *);
static glGetNamedBufferSubData_t real_glGetNamedBufferSubData;
typedef void (WINAPI *glFinish_t)(void);
typedef void (WINAPI *glFlush_t)(void);
typedef void (WINAPI *glClear_t)(GLbitfield);
typedef void (WINAPI *glBindFramebuffer_t)(GLenum, GLuint);
typedef void (WINAPI *glViewport_t)(GLint, GLint, GLsizei, GLsizei);
typedef void (WINAPI *glBlitFramebuffer_t)(GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum);
typedef void (WINAPI *glUniform4fv_t)(GLint, GLsizei, const float *);
typedef void (WINAPI *glUniform4f_t)(GLint, float, float, float, float);
typedef void (WINAPI *glActiveTexture_t)(GLenum);
typedef void (WINAPI *glBindTexture_t)(GLenum, GLuint);
typedef void (WINAPI *glBindTextures_t)(GLuint, GLsizei, const GLuint *);
typedef void (WINAPI *glBindTextureUnit_t)(GLuint, GLuint);
typedef void (WINAPI *glBindMultiTextureEXT_t)(GLenum, GLenum, GLuint);
typedef void (WINAPI *glGetTexImage_t)(GLenum, GLint, GLenum, GLenum, void *);
typedef void (WINAPI *glGetTextureImage_t)(GLuint, GLint, GLenum, GLenum, GLsizei, void *);
typedef void (WINAPI *glGetTextureLevelParameteriv_t)(GLuint, GLint, GLenum, GLint *);
typedef void (WINAPI *glGetIntegerv_t)(GLenum, GLint *);
typedef void (WINAPI *glReadPixels_t)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *);
static int g_uniform_logged;
static int g_fbo_dumped;
static void save_bmp(const char *path, int w, int h, const unsigned char *rgb);
static GLuint g_tex_unit0;
static GLuint g_tex_bind[48];

/* The BLEND_SHAPE vertex shaders read the current VBO through `g_vbo` (a
   fake GPU address packed into a vec4).  Remember the decoded buffer/offset
   so the draw-time wrapper can bind it as SSBO 29. */
static void capture_vbo_uniform(float x, float y)
{
    if (g_current_program >= 65536 || !g_prog_ptrs[g_current_program].needs_vbo_ssbo) return;
    uint32_t lo, hi;
    memcpy(&lo, &x, 4);
    memcpy(&hi, &y, 4);
    uint64_t addr = ((uint64_t)hi << 32) | lo;
    if (is_fake_addr(addr)) {
        decode_fake_addr(addr, &g_vbo_ssbo_buffer, &g_vbo_ssbo_offset);
        g_vbo_ssbo_valid = 1;
    }
}

/* The tile-light shaders get the light array GPU address from a uniform
   (g_constants[3].zw or g_constants[10].xy) packed as two floats.  Decode the
   fake address so apply_light_ssbo() can bind the real buffer as SSBO 36. */
static void capture_light_uniform(const float *value, int count)
{
    if (g_current_program >= 65536) return;
    if (!g_prog_ptrs[g_current_program].needs_light_ssbo &&
        !g_prog_ptrs[g_current_program].needs_raw_ssbo) return;
    if (!value || count <= 0) return;
    for (int i = 0; i < count; i++) {
        const float *v = value + (size_t)i * 4;
        uint32_t lo, hi;
        memcpy(&lo, &v[0], 4);
        memcpy(&hi, &v[1], 4);
        uint64_t a = ((uint64_t)hi << 32) | lo;
        if (is_fake_addr(a)) {
            GLuint off = 0;
            decode_fake_addr(a, &g_light_ssbo_buffer, &off);
            g_light_ssbo_offset = (GLintptr)off;
            g_light_ssbo_valid = 1;
            glog("LIGHT-CAP prog=%u lo=0x%x hi=0x%x buf=%u off=0x%x\n",
                 g_current_program, lo, hi, g_light_ssbo_buffer, (unsigned)g_light_ssbo_offset);
            return;
        }
        memcpy(&lo, &v[2], 4);
        memcpy(&hi, &v[3], 4);
        a = ((uint64_t)hi << 32) | lo;
        if (is_fake_addr(a)) {
            GLuint off = 0;
            decode_fake_addr(a, &g_light_ssbo_buffer, &off);
            g_light_ssbo_offset = (GLintptr)off;
            g_light_ssbo_valid = 1;
            glog("LIGHT-CAP prog=%u lo=0x%x hi=0x%x buf=%u off=0x%x\n",
                 g_current_program, lo, hi, g_light_ssbo_buffer, (unsigned)g_light_ssbo_offset);
            return;
        }
    }
}

static void WINAPI wrap_glUniform4fv(GLint location, GLsizei count, const float *value)
{
    static glUniform4fv_t real;
    if (!real) real = (glUniform4fv_t)trace_resolve("glUniform4fv");
    if (value && count > 0) capture_vbo_uniform(value[0], value[1]);
    if (value && count > 0) capture_light_uniform(value, count);
    if (value && g_uniform_logged < 4000) {
        glog("UNI4 prog=%u loc=%d cnt=%d val=(", g_current_program, location, count);
        int nv = count > 8 ? 32 : count * 4;
        for (int k = 0; k < nv; k++) glog("%s%.4f", k ? "," : "", value[k]);
        glog(")\n");
        g_uniform_logged++;
    }
    if (g_current_program == 15 && value && g_uniform_logged < 12) {
        glog("UNI4 prog=15 loc=%d cnt=%d val=(%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f)\n",
             location, count, value[0], value[1], value[2], value[3],
             count > 1 ? value[4] : 0, count > 1 ? value[5] : 0,
             count > 1 ? value[6] : 0, count > 1 ? value[7] : 0);
        g_uniform_logged++;
    }
    if (real) real(location, count, value);
}

static void dump_tex_image(GLenum target, GLuint tex)
{
    static glGetTextureImage_t real;
    static glGetTextureLevelParameteriv_t realp;
    if (!real) real = (glGetTextureImage_t)trace_resolve("glGetTextureImage");
    if (!realp) realp = (glGetTextureLevelParameteriv_t)trace_resolve("glGetTextureLevelParameteriv");
    if (!real || !realp) {
        glog("FBO dump_tex: resolve failed real=%d realp=%d\n", real != NULL, realp != NULL);
        return;
    }
    GLint w = 0, h = 0;
    realp(tex, 0, 0x1000 /* GL_TEXTURE_WIDTH */, &w);
    realp(tex, 0, 0x1001 /* GL_TEXTURE_HEIGHT */, &h);
    if (w <= 0 || h <= 0 || w > 8192 || h > 8192) {
        glog("FBO dump_tex: bad size tex=%u w=%d h=%d\n", tex, w, h);
        return;
    }
    unsigned char *buf = (unsigned char *)HeapAlloc(GetProcessHeap(), 0, (size_t)w * h * 3);
    if (!buf) return;
    memset(buf, 0, (size_t)w * h * 3);
    real(tex, 0, 0x1907 /* GL_RGB */, 0x1401 /* GL_UNSIGNED_BYTE */, w * h * 3, buf);
    char path[MAX_PATH];
    _snprintf(path, sizeof(path), "C:\\fgo\\_tools\\glshim\\fbo_%u_%dx%d.bmp", tex, w, h);
    save_bmp(path, w, h, buf);
    glog("FBO dumped tex=%u %dx%d\n", tex, w, h);
    HeapFree(GetProcessHeap(), 0, buf);
}

static void dump_tex_image_tag(GLenum target, GLuint tex, const char *tag)
{
    static glGetTextureImage_t dgi;
    static glGetTextureLevelParameteriv_t dlp;
    if (!dgi) dgi = (glGetTextureImage_t)trace_resolve("glGetTextureImage");
    if (!dlp) dlp = (glGetTextureLevelParameteriv_t)trace_resolve("glGetTextureLevelParameteriv");
    if (dgi && dlp) {
        GLint w = 0, h = 0, fmt = 0;
        dlp(tex, 0, 0x1000, &w);
        dlp(tex, 0, 0x1001, &h);
        dlp(tex, 0, 0x1003, &fmt);
        if (w > 0 && h > 0 && w <= 8192 && h <= 8192) {
            float *fbuf = (float *)HeapAlloc(GetProcessHeap(), 0, (size_t)w * h * 4 * sizeof(float));
            if (fbuf) {
                memset(fbuf, 0, (size_t)w * h * 4 * sizeof(float));
                dgi(tex, 0, 0x1908 /* GL_RGBA */, 0x1406 /* GL_FLOAT */,
                    (GLsizei)((size_t)w * h * 4 * sizeof(float)), fbuf);
                unsigned char *rgb = (unsigned char *)HeapAlloc(GetProcessHeap(), 0, (size_t)w * h * 3);
                if (rgb) {
                    for (int i = 0; i < w * h; i++) {
                        float r = fbuf[i * 4 + 0];
                        float g = fbuf[i * 4 + 1];
                        float b = fbuf[i * 4 + 2];
                        rgb[i * 3 + 0] = (unsigned char)(r < 0 ? 0 : (r > 1 ? 255 : (int)(r * 255.f)));
                        rgb[i * 3 + 1] = (unsigned char)(g < 0 ? 0 : (g > 1 ? 255 : (int)(g * 255.f)));
                        rgb[i * 3 + 2] = (unsigned char)(b < 0 ? 0 : (b > 1 ? 255 : (int)(b * 255.f)));
                    }
                    char path[MAX_PATH];
                    _snprintf(path, sizeof(path), "C:\\fgo\\_tools\\glshim\\tex_%s_%llu_%u_%dx%d.bmp",
                              tag, (unsigned long long)g_frame_count, tex, w, h);
                    save_bmp(path, w, h, rgb);
                    FILE *dbg = fopen("C:\\fgo\\_tools\\glshim\\texdump_debug.log", "a");
                    if (dbg) { fprintf(dbg, "DSA saved %s fmt=0x%x\n", path, fmt); fclose(dbg); }
                    HeapFree(GetProcessHeap(), 0, rgb);
                }
                HeapFree(GetProcessHeap(), 0, fbuf);
            }
            return;
        }
    }
    typedef void (WINAPI *glGetTexImage_t)(GLenum, GLint, GLenum, GLenum, void *);
    typedef void (WINAPI *glGetTexLevelParameteriv_t)(GLenum, GLint, GLenum, GLint *);
    typedef void (WINAPI *glBindTexture_t)(GLenum, GLuint);
    typedef void (WINAPI *glActiveTexture_t)(GLenum);
    typedef void (WINAPI *glGetIntegerv_t)(GLenum, GLint *);
    static glGetTexImage_t gt;
    static glGetTexLevelParameteriv_t glp;
    static glBindTexture_t bt;
    static glActiveTexture_t at;
    static glGetIntegerv_t giv;
    FILE *dbg = fopen("C:\\fgo\\_tools\\glshim\\texdump_debug.log", "a");
    if (dbg) {
        fprintf(dbg, "enter tex=%u tag=%s frame=%llu dumped=%d\n",
                tex, tag, (unsigned long long)g_frame_count, g_tex563_dumped);
    }
    if (!gt) gt = (glGetTexImage_t)GetProcAddress(g_real, "glGetTexImage");
    if (!glp) glp = (glGetTexLevelParameteriv_t)GetProcAddress(g_real, "glGetTexLevelParameteriv");
    if (!bt) bt = (glBindTexture_t)GetProcAddress(g_real, "glBindTexture");
    if (!at) at = (glActiveTexture_t)trace_resolve("glActiveTexture");
    if (!giv) giv = (glGetIntegerv_t)trace_resolve("glGetIntegerv");
    if (!gt || !glp || !bt || !at || !giv) {
        if (dbg) { fprintf(dbg, "resolve failed gt=%d glp=%d bt=%d at=%d giv=%d\n",
                           gt != NULL, glp != NULL, bt != NULL, at != NULL, giv != NULL); fclose(dbg); }
        return;
    }
    GLint prev_unit = 0, prev_tex = 0;
    giv(0x84E0 /* GL_ACTIVE_TEXTURE */, &prev_unit);
    at(0x84C0 /* GL_TEXTURE0 */);
    giv(0x8069 /* GL_TEXTURE_BINDING_2D */, &prev_tex);
    static const GLenum probe_targets[] = {
        0x0DE1, 0x8064, 0x84F5, 0x9100, 0x8513, 0x8515, 0x8517, 0x8519
    };
    GLenum used_target = 0;
    GLint w = 0, h = 0, d = 1;
    for (int ti = 0; ti < (int)(sizeof(probe_targets)/sizeof(probe_targets[0])); ti++) {
        bt(probe_targets[ti], tex);
        w = 0; h = 0;
        glp(probe_targets[ti], 0, 0x1000, &w);
        if (w > 0) {
            glp(probe_targets[ti], 0, 0x1001, &h);
            used_target = probe_targets[ti];
            break;
        }
    }
    if (dbg) fprintf(dbg, "target=0x%x size %dx%d\n", used_target, w, h);
    if (used_target == 0x8064) {
        d = 1;
        glp(used_target, 0, 0x8071 /* GL_TEXTURE_DEPTH */, &d);
        if (d < 1) d = 1;
    }
    if (w <= 0 || h <= 0 || w > 8192 || h > 8192) {
        bt(0x0DE1, (GLuint)prev_tex);
        at((GLenum)prev_unit);
        if (dbg) { fprintf(dbg, "bad size\n"); fclose(dbg); }
        return;
    }
    unsigned char *buf = (unsigned char *)HeapAlloc(GetProcessHeap(), 0, (size_t)w * h * d * 3);
    if (buf) {
        memset(buf, 0, (size_t)w * h * d * 3);
        gt(used_target, 0, 0x1907, 0x1401, buf);
        char path[MAX_PATH];
        _snprintf(path, sizeof(path), "C:\\fgo\\_tools\\glshim\\tex_%s_%llu_%u_%dx%d.bmp",
                  tag, (unsigned long long)g_frame_count, tex, w, h);
        save_bmp(path, w, h, buf);
        if (dbg) fprintf(dbg, "saved %s\n", path);
    }
    HeapFree(GetProcessHeap(), 0, buf);
    bt(0x0DE1, (GLuint)prev_tex);
    at((GLenum)prev_unit);
    if (dbg) fclose(dbg);
}

static void WINAPI wrap_glUniform4f(GLint location, float x, float y, float z, float w)
{
    static glUniform4f_t real;
    if (!real) real = (glUniform4f_t)trace_resolve("glUniform4f");
    capture_vbo_uniform(x, y);
    if (g_uniform_logged < 4000) {
        glog("UNI4f prog=%u loc=%d val=(%.4f,%.4f,%.4f,%.4f)\n",
             g_current_program, location, x, y, z, w);
        g_uniform_logged++;
    }
    if (g_current_program == 15 && g_uniform_logged < 12) {
        glog("UNI4f prog=15 loc=%d val=(%.4f,%.4f,%.4f,%.4f)\n", location, x, y, z, w);
        g_uniform_logged++;
    }
    if (real) real(location, x, y, z, w);
}

static void dump_framebuffer_readback(GLuint fb, const char *tag)
{
    static glBindFramebuffer_t rbf;
    static glGetIntegerv_t rgiv;
    static glReadPixels_t rrp;
    if (!rbf) rbf = (glBindFramebuffer_t)trace_resolve("glBindFramebuffer");
    if (!rgiv) rgiv = (glGetIntegerv_t)trace_resolve("glGetIntegerv");
    if (!rrp) rrp = (glReadPixels_t)trace_resolve("glReadPixels");
    if (!rbf || !rgiv || !rrp) return;
    GLint vp[4] = {0};
    rbf(0x8CA8 /* GL_READ_FRAMEBUFFER */, fb);
    rgiv(0x0BA2 /* GL_VIEWPORT */, vp);
    int w = vp[2], h = vp[3];
    if (w <= 0 || h <= 0 || w > 8192 || h > 8192) {
        rbf(0x8CA8, 0);
        return;
    }
    unsigned char *rgb = (unsigned char *)HeapAlloc(GetProcessHeap(), 0, (size_t)w * h * 3);
    if (rgb) {
        memset(rgb, 0, (size_t)w * h * 3);
        rrp(0, 0, w, h, 0x1907 /* GL_RGB */, 0x1401 /* GL_UNSIGNED_BYTE */, rgb);
        char path[MAX_PATH];
        _snprintf(path, sizeof(path), "C:\\fgo\\_tools\\glshim\\fbo_read_%s_%dx%d.bmp",
                  tag, w, h);
        save_bmp(path, w, h, rgb);
        glog("FBO readback fb=%u tag=%s %dx%d\n", fb, tag, w, h);
        HeapFree(GetProcessHeap(), 0, rgb);
    }
    rbf(0x8CA8, 0);
}

static void WINAPI wrap_glActiveTexture(GLenum unit)
{
    static glActiveTexture_t real;
    if (!real) real = (glActiveTexture_t)trace_resolve("glActiveTexture");
    g_active_texture_unit = unit;
    if (g_uniform_logged < 60) glog("TEX glActiveTexture unit=0x%x\n", unit);
    if (real) real(unit);
}

void WINAPI wrap_glBindTexture(GLenum target, GLuint texture)
{
    static glBindTexture_t real;
    if (!real) real = (glBindTexture_t)GetProcAddress(g_real, "glBindTexture");
    if (target == 0x0DE1) {
        if (g_active_texture_unit == 0x84C0) g_tex_unit0 = texture;
        else if (g_active_texture_unit == 0x84C1) g_tex_unit1 = texture;
        if (g_active_texture_unit >= 0x84C0 && g_active_texture_unit < 0x84C0 + 48)
            g_tex_bind[g_active_texture_unit - 0x84C0] = texture;
    }
    if (g_uniform_logged < 60) glog("TEX glBindTexture target=0x%x tex=%u\n", target, texture);
    if (real) real(target, texture);
}

static void WINAPI wrap_glBindTextures(GLuint first, GLsizei count, const GLuint *textures)
{
    static glBindTextures_t real;
    if (!real) real = (glBindTextures_t)trace_resolve("glBindTextures");
    if (textures) {
        for (GLsizei i = 0; i < count; i++) {
            GLuint u = first + (GLuint)i;
            if (u == 0) g_tex_unit0 = textures[i];
            else if (u == 1) g_tex_unit1 = textures[i];
            if (u < 48) g_tex_bind[u] = textures[i];
        }
    }
    if (g_uniform_logged < 60 && textures) {
        glog("TEX glBindTextures first=%u count=%d tex0=%u\n", first, count, textures[0]);
    }
    if (real) real(first, count, textures);
}

static void WINAPI wrap_glBindTextureUnit(GLuint unit, GLuint texture)
{
    static glBindTextureUnit_t real;
    if (!real) real = (glBindTextureUnit_t)trace_resolve("glBindTextureUnit");
    if (unit == 0) g_tex_unit0 = texture;
    else if (unit == 1) g_tex_unit1 = texture;
    if (unit < 48) g_tex_bind[unit] = texture;
    if (g_uniform_logged < 60) glog("TEX glBindTextureUnit unit=%u tex=%u\n", unit, texture);
    if (real) real(unit, texture);
}

static void WINAPI wrap_glBindMultiTextureEXT(GLenum texunit, GLenum target, GLuint texture)
{
    static glBindMultiTextureEXT_t real;
    if (!real) real = (glBindMultiTextureEXT_t)trace_resolve("glBindMultiTextureEXT");
    if (texunit == 0x84C0 && target == 0x0DE1) g_tex_unit0 = texture;
    if (texunit == 0x84C1 && target == 0x0DE1) g_tex_unit1 = texture;
    if (texunit >= 0x84C0 && texunit < 0x84C0 + 48 && target == 0x0DE1)
        g_tex_bind[texunit - 0x84C0] = texture;
    if (g_uniform_logged < 60) glog("TEX glBindMultiTextureEXT unit=0x%x target=0x%x tex=%u\n", texunit, target, texture);
    if (real) real(texunit, target, texture);
}

#define GL_ARRAY_BUFFER           0x8892
#define GL_ELEMENT_ARRAY_BUFFER   0x8893
#define GL_UNIFORM_BUFFER         0x8A11
#define GL_SHADER_STORAGE_BUFFER  0x90D2
#define GL_DRAW_INDIRECT_BUFFER   0x8F3F

static GLsizeiptr g_buffer_size[1 << 20];
static GLuint g_ubo_buffer[64];
static GLuint64 g_ubo_offset[64];
static GLuint64 g_ubo_length[64];
static GLuint g_ssbo_buffer[96];
static GLuint64 g_ssbo_offset[96];
static GLuint64 g_ssbo_length[96];

static PROC trace_resolve(const char *name)
{
    return real_wglGetProcAddress ? real_wglGetProcAddress(name) : NULL;
}

static void trace_buf_bind(GLenum target, GLuint buffer)
{
    if (target < 65536) g_bound_buffer[target] = buffer;
}

static void trace_buf_size(GLuint buffer, GLsizeiptr size)
{
    if (buffer > 0 && buffer < (1u << 20)) g_buffer_size[buffer] = size;
}

static void trace_ubo_bind(GLuint index, GLuint buffer, GLintptr offset, GLsizeiptr size)
{
    if (index < 64) {
        g_ubo_buffer[index] = buffer;
        g_ubo_offset[index] = (GLuint64)offset;
        g_ubo_length[index] = (GLuint64)size;
    }
}

static void trace_ssbo_bind(GLuint index, GLuint buffer, GLintptr offset, GLsizeiptr size)
{
    if (index < 96) {
        g_ssbo_buffer[index] = buffer;
        g_ssbo_offset[index] = (GLuint64)offset;
        g_ssbo_length[index] = (GLuint64)size;
    }
}

static void WINAPI wrap_glUseProgram(GLuint program)
{
    static glUseProgram_t real;
    if (!real) real = (glUseProgram_t)trace_resolve("glUseProgram");
    g_current_program = program;
    glog("PROG glUseProgram %u\n", program);
    if (real) real(program);
}

static void WINAPI wrap_glBindVertexArray(GLuint array)
{
    static glBindVertexArray_t real;
    if (!real) real = (glBindVertexArray_t)trace_resolve("glBindVertexArray");
    g_current_vao = array;
    if (array == 0) maybe_capture_frame();
    if (array == 0) bluefind_frame();
    glog("BIND glBindVertexArray %u\n", array);
    if (real) real(array);
}

static void WINAPI wrap_glBindBuffer(GLenum target, GLuint buffer)
{
    if (!real_glBindBuffer) ensure_gl_bind_buffer();
    glog("BIND glBindBuffer target=0x%x buf=%u\n", target, buffer);
    trace_buf_bind(target, buffer);
    if (real_glBindBuffer) real_glBindBuffer(target, buffer);
}

static void WINAPI wrap_glBindBufferBase(GLenum target, GLuint index, GLuint buffer)
{
    static glBindBufferBase_t real;
    if (!real) real = (glBindBufferBase_t)trace_resolve("glBindBufferBase");
    glog("BIND glBindBufferBase target=0x%x idx=%u buf=%u\n", target, index, buffer);
    if (target == GL_UNIFORM_BUFFER) trace_ubo_bind(index, buffer, 0, g_buffer_size[buffer]);
    if (target == GL_SHADER_STORAGE_BUFFER) trace_ssbo_bind(index, buffer, 0, g_buffer_size[buffer]);
    if (real) real(target, index, buffer);
}

static void WINAPI wrap_glBindBufferRange(GLenum target, GLuint index, GLuint buffer, GLintptr offset, GLsizeiptr size)
{
    static glBindBufferRange_t real;
    if (!real) real = (glBindBufferRange_t)trace_resolve("glBindBufferRange");
    glog("BIND glBindBufferRange target=0x%x idx=%u buf=%u off=%lld size=%lld\n",
         target, index, buffer, (long long)offset, (long long)size);
    if (target == GL_UNIFORM_BUFFER) trace_ubo_bind(index, buffer, offset, size);
    if (target == GL_SHADER_STORAGE_BUFFER) trace_ssbo_bind(index, buffer, offset, size);
    if (real) real(target, index, buffer, offset, size);
}

static void WINAPI wrap_glBufferData(GLenum target, GLsizeiptr size, const void *data, GLenum usage)
{
    static glBufferData_t real;
    if (!real) real = (glBufferData_t)trace_resolve("glBufferData");
    GLuint buf = (target < 65536) ? g_bound_buffer[target] : 0;
    glog("BUF glBufferData target=0x%x size=%lld usage=0x%x buf=%u\n",
         target, (long long)size, usage, buf);
    trace_buf_size(buf, size);
    if (real) real(target, size, data, usage);
}

static void WINAPI wrap_glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const void *data)
{
    static glBufferSubData_t real;
    if (!real) real = (glBufferSubData_t)trace_resolve("glBufferSubData");
    GLuint buf = (target < 65536) ? g_bound_buffer[target] : 0;
    glog("BUF glBufferSubData target=0x%x off=%lld size=%lld buf=%u\n",
         target, (long long)offset, (long long)size, buf);
    if (real) real(target, offset, size, data);
}

static void WINAPI wrap_glNamedBufferData(GLuint buffer, GLsizeiptr size, const void *data, GLenum usage)
{
    static glNamedBufferData_t real;
    if (!real) real = (glNamedBufferData_t)trace_resolve("glNamedBufferData");
    glog("BUF glNamedBufferData buf=%u size=%lld usage=0x%x\n", buffer, (long long)size, usage);
    trace_buf_size(buffer, size);
    if (real) real(buffer, size, data, usage);
}

static void WINAPI wrap_glNamedBufferSubData(GLuint buffer, GLintptr offset, GLsizeiptr size, const void *data)
{
    static glNamedBufferSubData_t real;
    if (!real) real = (glNamedBufferSubData_t)trace_resolve("glNamedBufferSubData");
    glog("BUF glNamedBufferSubData buf=%u off=%lld size=%lld\n", buffer, (long long)offset, (long long)size);
    if (real) real(buffer, offset, size, data);
}

static void WINAPI wrap_glBufferStorage(GLenum target, GLsizeiptr size, const void *data, GLbitfield flags)
{
    static glBufferStorage_t real;
    if (!real) real = (glBufferStorage_t)trace_resolve("glBufferStorage");
    GLuint buf = (target < 65536) ? g_bound_buffer[target] : 0;
    glog("BUF glBufferStorage target=0x%x size=%lld flags=0x%x buf=%u\n",
         target, (long long)size, flags, buf);
    trace_buf_size(buf, size);
    if (real) real(target, size, data, flags);
}

static void WINAPI wrap_glNamedBufferStorage(GLuint buffer, GLsizeiptr size, const void *data, GLbitfield flags)
{
    static glNamedBufferStorage_t real;
    if (!real) real = (glNamedBufferStorage_t)trace_resolve("glNamedBufferStorage");
    glog("BUF glNamedBufferStorage buf=%u size=%lld flags=0x%x\n", buffer, (long long)size, flags);
    trace_buf_size(buffer, size);
    if (real) real(buffer, size, data, flags);
}

static void * WINAPI wrap_glMapBuffer(GLenum target, GLenum access)
{
    static glMapBuffer_t real;
    if (!real) real = (glMapBuffer_t)trace_resolve("glMapBuffer");
    GLuint buf = (target < 65536) ? g_bound_buffer[target] : 0;
    glog("BUF glMapBuffer target=0x%x access=0x%x buf=%u\n", target, access, buf);
    return real ? real(target, access) : NULL;
}

static void * WINAPI wrap_glMapBufferRange(GLenum target, GLintptr offset, GLsizeiptr length, GLbitfield access)
{
    static glMapBufferRange_t real;
    if (!real) real = (glMapBufferRange_t)trace_resolve("glMapBufferRange");
    GLuint buf = (target < 65536) ? g_bound_buffer[target] : 0;
    glog("BUF glMapBufferRange target=0x%x off=%lld len=%lld access=0x%x buf=%u\n",
         target, (long long)offset, (long long)length, access, buf);
    return real ? real(target, offset, length, access) : NULL;
}

static void * WINAPI wrap_glMapNamedBuffer(GLuint buffer, GLenum access)
{
    static glMapNamedBuffer_t real;
    if (!real) real = (glMapNamedBuffer_t)trace_resolve("glMapNamedBuffer");
    glog("BUF glMapNamedBuffer buf=%u access=0x%x\n", buffer, access);
    return real ? real(buffer, access) : NULL;
}

static void * WINAPI wrap_glMapNamedBufferRange(GLuint buffer, GLintptr offset, GLsizeiptr length, GLbitfield access)
{
    static glMapNamedBufferRange_t real;
    if (!real) real = (glMapNamedBufferRange_t)trace_resolve("glMapNamedBufferRange");
    glog("BUF glMapNamedBufferRange buf=%u off=%lld len=%lld access=0x%x\n",
         buffer, (long long)offset, (long long)length, access);
    return real ? real(buffer, offset, length, access) : NULL;
}

static void WINAPI wrap_glVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void *pointer)
{
    static glVertexAttribPointer_t real;
    if (!real) real = (glVertexAttribPointer_t)trace_resolve("glVertexAttribPointer");
    glog("ATTRIB glVertexAttribPointer idx=%u size=%d type=0x%x norm=%d stride=%d ptr=%p\n",
         index, size, type, normalized, stride, pointer);
    if (real) real(index, size, type, normalized, stride, pointer);
}

static void WINAPI wrap_glVertexAttribFormat(GLuint attribindex, GLint size, GLenum type, GLboolean normalized, GLuint relativeoffset)
{
    static glVertexAttribFormat_t real;
    if (!real) real = (glVertexAttribFormat_t)trace_resolve("glVertexAttribFormat");
    if (attribindex < NV_MAX_ATTRIBS) {
        g_fmt[attribindex].set = 1;
        g_fmt[attribindex].size = size;
        g_fmt[attribindex].type = type;
        g_fmt[attribindex].normalized = normalized;
        g_fmt[attribindex].stride = 0;
        g_fmt[attribindex].relativeoffset = relativeoffset;
        g_fmt[attribindex].is_int = 0;
    }
    glog("ATTRIB glVertexAttribFormat idx=%u size=%d type=0x%x norm=%d rel=%u\n",
         attribindex, size, type, normalized, relativeoffset);
    if (real) real(attribindex, size, type, normalized, relativeoffset);
}

static void WINAPI wrap_glVertexAttribBinding(GLuint attribindex, GLuint bindingindex)
{
    static glVertexAttribBinding_t real;
    if (!real) real = (glVertexAttribBinding_t)trace_resolve("glVertexAttribBinding");
    if (attribindex < NV_MAX_ATTRIBS) {
        g_abind[attribindex].set = 1;
        g_abind[attribindex].binding = bindingindex;
    }
    glog("ATTRIB glVertexAttribBinding idx=%u binding=%u\n", attribindex, bindingindex);
    if (real) real(attribindex, bindingindex);
}

static void WINAPI wrap_glBindVertexBuffer(GLuint bindingindex, GLuint buffer, GLintptr offset, GLsizei stride)
{
    if (!real_glBindVertexBuffer) ensure_gl_bind_vertex_buffer();
    if (bindingindex < NV_MAX_ATTRIBS) {
        g_vbuf[bindingindex].set = 1;
        g_vbuf[bindingindex].buffer = buffer;
        g_vbuf[bindingindex].offset = offset;
        g_vbuf[bindingindex].stride = stride;
    }
    glog("ATTRIB glBindVertexBuffer binding=%u buf=%u off=%lld stride=%d\n",
         bindingindex, buffer, (long long)offset, stride);
    if (real_glBindVertexBuffer) real_glBindVertexBuffer(bindingindex, buffer, offset, stride);
}

static void WINAPI wrap_glEnableVertexAttribArray(GLuint index)
{
    static glEnableVertexAttribArray_t real;
    if (!real) real = (glEnableVertexAttribArray_t)trace_resolve("glEnableVertexAttribArray");
    glog("ATTRIB glEnableVertexAttribArray %u\n", index);
    if (index < 32) g_enabled_mask |= (1u << index);
    if (real) real(index);
}

static void WINAPI wrap_glDisableVertexAttribArray(GLuint index)
{
    static glDisableVertexAttribArray_t real;
    if (!real) real = (glDisableVertexAttribArray_t)trace_resolve("glDisableVertexAttribArray");
    glog("ATTRIB glDisableVertexAttribArray %u\n", index);
    if (index < 32) g_enabled_mask &= ~(1u << index);
    if (real) real(index);
}

static void WINAPI wrap_glVertexAttribIPointer(GLuint index, GLint size, GLenum type, GLsizei stride, const void *pointer)
{
    static glVertexAttribIPointer_t real;
    if (!real) real = (glVertexAttribIPointer_t)trace_resolve("glVertexAttribIPointer");
    glog("ATTRIB glVertexAttribIPointer idx=%u size=%d type=0x%x stride=%d ptr=%p\n",
         index, size, type, stride, pointer);
    if (real) real(index, size, type, stride, pointer);
}

static void WINAPI wrap_glVertexAttribIFormat(GLuint attribindex, GLint size, GLenum type, GLuint relativeoffset)
{
    static glVertexAttribIFormat_t real;
    if (!real) real = (glVertexAttribIFormat_t)trace_resolve("glVertexAttribIFormat");
    if (attribindex < NV_MAX_ATTRIBS) {
        g_fmt[attribindex].set = 1;
        g_fmt[attribindex].size = size;
        g_fmt[attribindex].type = type;
        g_fmt[attribindex].normalized = 0;
        g_fmt[attribindex].stride = 0;
        g_fmt[attribindex].relativeoffset = relativeoffset;
        g_fmt[attribindex].is_int = 1;
    }
    glog("ATTRIB glVertexAttribIFormat idx=%u size=%d type=0x%x rel=%u\n",
         attribindex, size, type, relativeoffset);
    if (real) real(attribindex, size, type, relativeoffset);
}

static void WINAPI wrap_glVertexAttribLFormat(GLuint attribindex, GLint size, GLenum type, GLuint relativeoffset)
{
    static glVertexAttribIFormat_t real;
    if (!real) real = (glVertexAttribIFormat_t)trace_resolve("glVertexAttribLFormat");
    if (attribindex < NV_MAX_ATTRIBS) {
        g_fmt[attribindex].set = 1;
        g_fmt[attribindex].size = size;
        g_fmt[attribindex].type = type;
        g_fmt[attribindex].normalized = 0;
        g_fmt[attribindex].stride = 0;
        g_fmt[attribindex].relativeoffset = relativeoffset;
        g_fmt[attribindex].is_int = 0;
    }
    glog("ATTRIB glVertexAttribLFormat idx=%u size=%d type=0x%x rel=%u\n",
         attribindex, size, type, relativeoffset);
    if (real) real(attribindex, size, type, relativeoffset);
}

/* DSA variants (GL 4.3+); the game binds VAO 0, so record into the same tables. */
static void WINAPI wrap_glVertexArrayAttribFormat(GLuint vao, GLuint attribindex, GLint size, GLenum type, GLboolean normalized, GLuint relativeoffset)
{
    static glVertexArrayAttribFormat_t real;
    if (!real) real = (glVertexArrayAttribFormat_t)trace_resolve("glVertexArrayAttribFormat");
    if (attribindex < NV_MAX_ATTRIBS && vao == 0) {
        g_fmt[attribindex].set = 1;
        g_fmt[attribindex].size = size;
        g_fmt[attribindex].type = type;
        g_fmt[attribindex].normalized = normalized;
        g_fmt[attribindex].stride = 0;
        g_fmt[attribindex].relativeoffset = relativeoffset;
        g_fmt[attribindex].is_int = 0;
    }
    glog("DSA glVertexArrayAttribFormat vao=%u idx=%u size=%d type=0x%x norm=%d rel=%u\n",
         vao, attribindex, size, type, normalized, relativeoffset);
    if (real) real(vao, attribindex, size, type, normalized, relativeoffset);
}

static void WINAPI wrap_glVertexArrayAttribIFormat(GLuint vao, GLuint attribindex, GLint size, GLenum type, GLuint relativeoffset)
{
    static glVertexArrayAttribIFormat_t real;
    if (!real) real = (glVertexArrayAttribIFormat_t)trace_resolve("glVertexArrayAttribIFormat");
    if (attribindex < NV_MAX_ATTRIBS && vao == 0) {
        g_fmt[attribindex].set = 1;
        g_fmt[attribindex].size = size;
        g_fmt[attribindex].type = type;
        g_fmt[attribindex].normalized = 0;
        g_fmt[attribindex].stride = 0;
        g_fmt[attribindex].relativeoffset = relativeoffset;
        g_fmt[attribindex].is_int = 1;
    }
    glog("DSA glVertexArrayAttribIFormat vao=%u idx=%u size=%d type=0x%x rel=%u\n",
         vao, attribindex, size, type, relativeoffset);
    if (real) real(vao, attribindex, size, type, relativeoffset);
}

static void WINAPI wrap_glVertexArrayAttribLFormat(GLuint vao, GLuint attribindex, GLint size, GLenum type, GLuint relativeoffset)
{
    static glVertexArrayAttribIFormat_t real;
    if (!real) real = (glVertexArrayAttribIFormat_t)trace_resolve("glVertexArrayAttribLFormat");
    if (attribindex < NV_MAX_ATTRIBS && vao == 0) {
        g_fmt[attribindex].set = 1;
        g_fmt[attribindex].size = size;
        g_fmt[attribindex].type = type;
        g_fmt[attribindex].normalized = 0;
        g_fmt[attribindex].stride = 0;
        g_fmt[attribindex].relativeoffset = relativeoffset;
        g_fmt[attribindex].is_int = 0;
    }
    glog("DSA glVertexArrayAttribLFormat vao=%u idx=%u size=%d type=0x%x rel=%u\n",
         vao, attribindex, size, type, relativeoffset);
    if (real) real(vao, attribindex, size, type, relativeoffset);
}

static void WINAPI wrap_glVertexArrayAttribBinding(GLuint vao, GLuint attribindex, GLuint bindingindex)
{
    static glVertexArrayAttribBinding_t real;
    if (!real) real = (glVertexArrayAttribBinding_t)trace_resolve("glVertexArrayAttribBinding");
    if (attribindex < NV_MAX_ATTRIBS && vao == 0) {
        g_abind[attribindex].set = 1;
        g_abind[attribindex].binding = bindingindex;
    }
    glog("DSA glVertexArrayAttribBinding vao=%u idx=%u binding=%u\n", vao, attribindex, bindingindex);
    if (real) real(vao, attribindex, bindingindex);
}

static void WINAPI wrap_glVertexArrayVertexBuffer(GLuint vao, GLuint bindingindex, GLuint buffer, GLintptr offset, GLsizei stride)
{
    static glVertexArrayVertexBuffer_t real;
    if (!real) real = (glVertexArrayVertexBuffer_t)trace_resolve("glVertexArrayVertexBuffer");
    if (bindingindex < NV_MAX_ATTRIBS && vao == 0) {
        g_vbuf[bindingindex].set = 1;
        g_vbuf[bindingindex].buffer = buffer;
        g_vbuf[bindingindex].offset = offset;
        g_vbuf[bindingindex].stride = stride;
    }
    glog("DSA glVertexArrayVertexBuffer vao=%u binding=%u buf=%u off=%lld stride=%d\n",
         vao, bindingindex, buffer, (long long)offset, stride);
    if (real) real(vao, bindingindex, buffer, offset, stride);
}

static void WINAPI wrap_glEnableVertexArrayAttrib(GLuint vao, GLuint index)
{
    static glEnableVertexArrayAttrib_t real;
    if (!real) real = (glEnableVertexArrayAttrib_t)trace_resolve("glEnableVertexArrayAttrib");
    if (vao == 0 && index < 32) g_enabled_mask |= (1u << index);
    glog("DSA glEnableVertexArrayAttrib vao=%u idx=%u\n", vao, index);
    if (real) real(vao, index);
}

static void WINAPI wrap_glDisableVertexArrayAttrib(GLuint vao, GLuint index)
{
    static glEnableVertexArrayAttrib_t real;
    if (!real) real = (glEnableVertexArrayAttrib_t)trace_resolve("glDisableVertexArrayAttrib");
    if (vao == 0 && index < 32) g_enabled_mask &= ~(1u << index);
    glog("DSA glDisableVertexArrayAttrib vao=%u idx=%u\n", vao, index);
    if (real) real(vao, index);
}

typedef void (WINAPI *glEnableClientState_t)(GLenum);
static glEnableClientState_t real_glEnableClientState;
static glEnableClientState_t real_glDisableClientState;

void WINAPI wrap_glEnableClientState(GLenum cap)
{
    if (!real_glEnableClientState) real_glEnableClientState = (glEnableClientState_t)GetProcAddress(g_real, "glEnableClientState");
    if (cap == GL_VERTEX_ATTRIB_ARRAY_UNIFIED_NV) {
        g_unified_attrib_state = 1;
        glog("STATE glEnableClientState VERTEX_ATTRIB_ARRAY_UNIFIED_NV\n");
    } else if (cap == GL_ELEMENT_ARRAY_UNIFIED_NV) {
        g_unified_elem_state = 1;
        glog("STATE glEnableClientState ELEMENT_ARRAY_UNIFIED_NV\n");
    } else if (cap == GL_UNIFORM_BUFFER_UNIFIED_NV) {
        g_unified_ubo_state = 1;
        glog("STATE glEnableClientState UNIFORM_BUFFER_UNIFIED_NV\n");
    } else {
        glog("STATE glEnableClientState 0x%x\n", cap);
    }
    if (real_glEnableClientState) real_glEnableClientState(cap);
}

void WINAPI wrap_glDisableClientState(GLenum cap)
{
    if (!real_glDisableClientState) real_glDisableClientState = (glEnableClientState_t)GetProcAddress(g_real, "glDisableClientState");
    if (cap == GL_VERTEX_ATTRIB_ARRAY_UNIFIED_NV) {
        g_unified_attrib_state = 0;
        glog("STATE glDisableClientState VERTEX_ATTRIB_ARRAY_UNIFIED_NV\n");
    } else if (cap == GL_ELEMENT_ARRAY_UNIFIED_NV) {
        g_unified_elem_state = 0;
        glog("STATE glDisableClientState ELEMENT_ARRAY_UNIFIED_NV\n");
    } else if (cap == GL_UNIFORM_BUFFER_UNIFIED_NV) {
        g_unified_ubo_state = 0;
        glog("STATE glDisableClientState UNIFORM_BUFFER_UNIFIED_NV\n");
    } else {
        glog("STATE glDisableClientState 0x%x\n", cap);
    }
    if (real_glDisableClientState) real_glDisableClientState(cap);
}

static int g_draw_details;
static int g_perdraw_full;
static int g_scene_dumped;

static void record_scene_ring(const char *fn, GLenum mode, GLint first,
                              GLsizei count, GLsizei primcount)
{
    (void)fn;
    scene_rec *r = &g_scene_ring[g_scene_ring_pos];
    r->frame = g_frame_count;
    r->prog = g_current_program;
    r->mode = mode;
    r->first = first;
    r->count = count;
    r->primcount = primcount;
    r->vao = g_current_vao;
    r->tex0 = g_tex_unit0;
    r->tex1 = g_tex_unit1;
    r->ubo0 = g_ubo_buffer[0];
    r->ubo1 = g_ubo_buffer[1];
    r->ubo2 = g_ubo_buffer[2];
    r->ubo3 = g_ubo_buffer[3];
    r->ssbo0 = g_ssbo_buffer[0];
    r->ssbo1 = g_ssbo_buffer[1];
    r->ssbo2 = g_ssbo_buffer[2];
    r->ssbo3 = g_ssbo_buffer[3];
    r->vp[0] = r->vp[1] = r->vp[2] = r->vp[3] = 0;
    {
        static glGetIntegerv_t giv;
        if (!giv) giv = (glGetIntegerv_t)trace_resolve("glGetIntegerv");
        if (giv) giv(0x0BA2 /* GL_VIEWPORT */, r->vp);
    }
    r->nv_attr_mask = 0;
    for (int i = 0; i < NV_MAX_ATTRIBS; i++)
        if (g_nv_va[i].addr != 0) r->nv_attr_mask |= (1u << i);
    g_scene_ring_pos++;
    if (g_scene_ring_pos >= SCENE_RING_MAX) {
        g_scene_ring_pos = 0;
        g_scene_ring_full = 1;
    }
}

static void dump_scene_ring(const char *why)
{
    FILE *f = fopen("C:\\fgo\\_tools\\glshim\\blue_scene.log", "a");
    if (!f) return;
    fprintf(f, "=== SCENE %s frame=%llu prog=%u pos=%d full=%d ===\n",
            why, (unsigned long long)g_frame_count, g_current_program,
            g_scene_ring_pos, g_scene_ring_full);
    int n = g_scene_ring_full ? SCENE_RING_MAX : g_scene_ring_pos;
    int start = g_scene_ring_full ? g_scene_ring_pos : 0;
    for (int k = 0; k < n; k++) {
        scene_rec *r = &g_scene_ring[(start + k) % SCENE_RING_MAX];
        fprintf(f,
                "R%03d frame=%llu prog=%u mode=0x%x first=%d count=%d prim=%d "
                "vao=%u tex0=%u tex1=%u ubo=(%u,%u,%u,%u) ssbo=(%u,%u,%u,%u) "
                "vp=(%d,%d,%d,%d) nvattr=0x%x\n",
                k, (unsigned long long)r->frame, r->prog, r->mode, r->first,
                r->count, r->primcount, r->vao, r->tex0, r->tex1,
                r->ubo0, r->ubo1, r->ubo2, r->ubo3,
                r->ssbo0, r->ssbo1, r->ssbo2, r->ssbo3,
                r->vp[0], r->vp[1], r->vp[2], r->vp[3], r->nv_attr_mask);
    }
    fclose(f);
}

/* Bind SSBO 29 to the VBO captured from g_vbo for BLEND_SHAPE programs. */
static void apply_vbo_ssbo(void)
{
    if (g_current_program >= 65536 || !g_prog_ptrs[g_current_program].needs_vbo_ssbo) return;
    if (!g_vbo_ssbo_valid || !g_vbo_ssbo_buffer) return;
    static glBindBufferRange_t real;
    if (!real) real = (glBindBufferRange_t)trace_resolve("glBindBufferRange");
    if (!real) return;
    GLsizeiptr len = (g_vbo_ssbo_buffer < (1u << 20) &&
                      g_buffer_size[g_vbo_ssbo_buffer] > (GLintptr)g_vbo_ssbo_offset)
                         ? (GLsizeiptr)(g_buffer_size[g_vbo_ssbo_buffer] - g_vbo_ssbo_offset)
                         : 0;
    real(GL_SHADER_STORAGE_BUFFER, 29, g_vbo_ssbo_buffer, (GLintptr)g_vbo_ssbo_offset, len);
    glog("VBO-SSBO prog=%u bind29 buf=%u off=0x%x len=%lld\n",
         g_current_program, g_vbo_ssbo_buffer, g_vbo_ssbo_offset, (long long)len);
}

/* Bind SSBO 36 to the light buffer captured from the g_light_gpu_addr /
   g_lights_addr uniform for the tile-light shaders (previously PTRREJECTed). */
static void apply_light_ssbo(void)
{
    if (g_current_program >= 65536) return;
    int need_light = g_prog_ptrs[g_current_program].needs_light_ssbo;
    int need_raw = g_prog_ptrs[g_current_program].needs_raw_ssbo;
    if (!need_light && !need_raw) return;
    if (!g_light_ssbo_valid || !g_light_ssbo_buffer) return;
    static glBindBufferRange_t real;
    if (!real) real = (glBindBufferRange_t)trace_resolve("glBindBufferRange");
    if (!real) return;
    GLsizeiptr len = (g_light_ssbo_buffer < (1u << 20) &&
                      g_buffer_size[g_light_ssbo_buffer] > (GLintptr)g_light_ssbo_offset)
                         ? (GLsizeiptr)(g_buffer_size[g_light_ssbo_buffer] - g_light_ssbo_offset)
                         : 0;
    if (need_light) {
        real(GL_SHADER_STORAGE_BUFFER, 36, g_light_ssbo_buffer, (GLintptr)g_light_ssbo_offset, len);
        glog("LIGHT-SSBO prog=%u bind36 buf=%u off=0x%x len=%lld\n",
             g_current_program, g_light_ssbo_buffer, (GLintptr)g_light_ssbo_offset, (long long)len);
    }
    if (need_raw) {
        real(GL_SHADER_STORAGE_BUFFER, 37, g_light_ssbo_buffer, (GLintptr)g_light_ssbo_offset, len);
        glog("RAW-SSBO prog=%u bind37 buf=%u off=0x%x len=%lld\n",
             g_current_program, g_light_ssbo_buffer, (GLintptr)g_light_ssbo_offset, (long long)len);
    }
}

/* Translate GL_NV_vertex_buffer_unified_memory attribute state into regular
   VAO state just before a draw.  The game only ever binds VAO 0. */
static void apply_unified_attribs(void)
{
    apply_pointer_bindings();
    apply_vbo_ssbo();
    apply_light_ssbo();
    /* NV_vertex_buffer_unified_memory also replaces the GL_ELEMENT_ARRAY_BUFFER
       binding.  The game sets GL_ELEMENT_ARRAY_ADDRESS_NV (buf 91 here) but the
       shim never bound it, so every indexed draw (UI text!) read garbage
       indices.  Bind the decoded buffer before indexed draws. */
    if (g_nv_ea.addr != 0) {
        GLuint eb = 0, eo = 0;
        decode_fake_addr(g_nv_ea.addr, &eb, &eo);
        if (eb != 0 && eb < (1u << 20)) {
            static glBindBuffer_t bb;
            if (!bb) bb = (glBindBuffer_t)trace_resolve("glBindBuffer");
            if (bb) {
                bb(GL_ELEMENT_ARRAY_BUFFER, eb);
                g_nv_ea_buf = eb;
                g_nv_ea_off = (GLintptr)eo;
                g_nv_ea_valid = 1;
            }
        }
    }
    /* prog 623 (outline composite) renders into fb94/tex99 while sampling
       tex99 on unit 0 -- a feedback loop that NVIDIA tolerates (reads the
       pre-draw content) but AMD turns into frame-varying garbage.  The game
       copies tex99 -> tex118 every frame, so redirect unit 0 to tex118 to
       replicate the NVIDIA read-before-write behavior. */
    if (g_current_program == 623 && g_tex_unit0 == 99) {
        static void (WINAPI *btu)(GLuint, GLuint);
        static int fb_logged;
        if (!btu) btu = (void (WINAPI *)(GLuint, GLuint))trace_resolve("glBindTextureUnit");
        if (btu) {
            btu(0, 118);
            if (fb_logged < 10) {
                fb_logged++;
                glog("FEEDBACK-FIX prog=623 unit0 tex99 -> tex118 (frame=%llu)\n",
                     (unsigned long long)g_frame_count);
            }
        }
    }
    static glBindVertexBuffer_t fvb;
    static glVertexAttribFormat_t fvf;
    static glVertexAttribIFormat_t fvif;
    static glVertexAttribBinding_t fvbnd;
    static glEnableVertexAttribArray_t fe;
    static glDisableVertexAttribArray_t fd;
    if (!fvb) fvb = (glBindVertexBuffer_t)trace_resolve("glBindVertexBuffer");
    if (!fvf) fvf = (glVertexAttribFormat_t)trace_resolve("glVertexAttribFormat");
    if (!fvif) fvif = (glVertexAttribIFormat_t)trace_resolve("glVertexAttribIFormat");
    if (!fvbnd) fvbnd = (glVertexAttribBinding_t)trace_resolve("glVertexAttribBinding");
    if (!fe) fe = (glEnableVertexAttribArray_t)trace_resolve("glEnableVertexAttribArray");
    if (!fd) fd = (glDisableVertexAttribArray_t)trace_resolve("glDisableVertexAttribArray");

    GLuint want = 0;
    for (int i = 0; i < NV_MAX_ATTRIBS; i++) {
        if (g_nv_va_keep[i].addr != 0) want |= (1u << i);
    }

    for (int i = 0; i < NV_MAX_ATTRIBS; i++) {
        GLuint bit = 1u << i;
        if (want & bit) {
            GLuint buf = 0, off = 0;
            decode_fake_addr(g_nv_va_keep[i].addr, &buf, &off);
            if (buf == 0 || buf >= (1u << 20)) continue;
            int bind = g_abind[i].set ? (int)g_abind[i].binding : (int)i;
            if (bind < 0 || bind >= NV_MAX_ATTRIBS) bind = (int)i;
            /* Stride/offset come from the game's VAO vertex-buffer binding when
               present (ARB glVertexAttribFormat carries no stride; the NV format
               stride is 0 when the game uses glBindVertexBuffer). */
            GLsizei stride = 0;
            if (g_vbuf[bind].set) stride = g_vbuf[bind].stride;
            if (stride == 0 && g_fmt[i].set) stride = g_fmt[i].stride;
            GLintptr base = (GLintptr)off;
            if (g_vbuf[bind].set) base += g_vbuf[bind].offset;
            if (g_fmt[i].set) base += (GLintptr)g_fmt[i].relativeoffset;
            if (g_fmt[i].set && fvf && fvb) {
                if (g_fmt[i].is_int && fvif) fvif(i, g_fmt[i].size, g_fmt[i].type, 0);
                else fvf(i, g_fmt[i].size, g_fmt[i].type, g_fmt[i].normalized, 0);
            }
            if (fvb) fvb(i, buf, base, stride);
            if (fvbnd) fvbnd(i, i);
            if (fe) fe(i);
            g_emitted_mask |= bit;
        } else if ((g_emitted_mask & bit) && !(g_enabled_mask & bit)) {
            /* The game cleared the NV range for this attribute.  Only mirror
               the disabled state when the game itself has not enabled the
               attribute through its own VAO state; attributes configured once
               at init (NV range + regular enable) must survive the per-frame
               NV reset, otherwise the scene passes draw with no vertex data. */
            if (fd) fd(i);
            g_emitted_mask &= ~bit;
        }
    }
}

static void ui_diag(int mode, const GLint *first, const GLsizei *count, int first_override);

static void draw_log(const char *fn, GLenum mode, GLint first, GLsizei count, GLsizei primcount, const void *extra)
{
    record_scene_ring(fn, mode, first, count, primcount);
    if (g_bound_draw_framebuffer != 0 && g_bound_draw_framebuffer < 65536) {
        static int rt_logged;
        if (rt_logged < 200) {
            rt_logged++;
            FILE *f = fopen("C:\\fgo\\_tools\\glshim\\swap.log", "a");
            if (f) {
                fprintf(f, "[%llu] RENDERTO prog=%u fb=%u tex=[%u,%u,%u,%u]\n",
                        (unsigned long long)g_frame_count, g_current_program,
                        g_bound_draw_framebuffer,
                        g_fbo_attach[g_bound_draw_framebuffer][0],
                        g_fbo_attach[g_bound_draw_framebuffer][1],
                        g_fbo_attach[g_bound_draw_framebuffer][2],
                        g_fbo_attach[g_bound_draw_framebuffer][3]);
                fclose(f);
            }
        }
        if (g_bound_draw_framebuffer >= 90) {
            static int fbo_status_logged[256];
            int fbi = (int)g_bound_draw_framebuffer - 90;
            if (fbi >= 0 && fbi < 256 && !fbo_status_logged[fbi]) {
                fbo_status_logged[fbi] = 1;
                typedef GLenum (WINAPI *glCheckFramebufferStatus_t)(GLenum);
                typedef void (WINAPI *glGetIntegerv_t)(GLenum, GLint *);
                static glCheckFramebufferStatus_t cfs;
                static glGetIntegerv_t gi;
                if (!cfs) cfs = (glCheckFramebufferStatus_t)trace_resolve("glCheckFramebufferStatus");
                if (!gi) gi = (glGetIntegerv_t)trace_resolve("glGetIntegerv");
                if (cfs && gi) {
                    GLenum st = cfs(0x8D40 /* GL_FRAMEBUFFER */);
                    GLint db0 = -1, cm[4] = {-1,-1,-1,-1}, dm = -1, vp[4] = {0,0,0,0};
                    gi(0x0C01 /* GL_DRAW_BUFFER0 */, &db0);
                    gi(0x0C23 /* GL_COLOR_WRITEMASK */, cm);
                    gi(0x0B72 /* GL_DEPTH_WRITEMASK */, &dm);
                    gi(0x0BA2 /* GL_VIEWPORT */, vp);
                    state_log("FBOCHECK fb=%u status=0x%x db0=0x%x colormask=%d,%d,%d,%d depthmask=%d vp=%d,%d,%d,%d prog=%u",
                              g_bound_draw_framebuffer, st, db0, cm[0], cm[1], cm[2], cm[3], dm,
                              vp[0], vp[1], vp[2], vp[3], g_current_program);
                }
            }
        }
        if (g_current_program < 8192) {
            static int prog_drawtex_logged[8192];
            if (prog_drawtex_logged[g_current_program] < 2) {
                prog_drawtex_logged[g_current_program]++;
                typedef void (WINAPI *glGetIntegerv_t)(GLenum, GLint *);
                static glGetIntegerv_t gi;
                if (!gi) gi = (glGetIntegerv_t)trace_resolve("glGetIntegerv");
                GLint vp[4] = {0,0,0,0};
                if (gi) gi(0x0BA2, vp);
                state_log("DRAWTEX prog=%u fb=%u tex=[%u,%u,%u,%u] ubo=[%u,%u,%u,%u] ssbo=[%u,%u,%u,%u] vao=%u vp=%d,%d,%d,%d mode=0x%x count=%d",
                          g_current_program, g_bound_draw_framebuffer,
                          g_tex_bind[0], g_tex_bind[1], g_tex_bind[2], g_tex_bind[3],
                          g_ubo_buffer[0], g_ubo_buffer[1], g_ubo_buffer[2], g_ubo_buffer[3],
                          g_ssbo_buffer[0], g_ssbo_buffer[1], g_ssbo_buffer[2], g_ssbo_buffer[3],
                          g_current_vao, vp[0], vp[1], vp[2], vp[3], mode, count);
            }
        }
    }
    if (g_current_program == 623 && GetFileAttributesA("C:\\fgo\\_tools\\glshim\\dumpnow.on") != INVALID_FILE_ATTRIBUTES) {
        /* one-shot deep dump at the scene-composite draw (fb94/tex99 target):
           verify whether the game samples tex99 while writing tex99 (feedback). */
        typedef void (WINAPI *glGetFramebufferAttachmentParameteriv_t)(GLenum, GLenum, GLenum, GLint *);
        typedef void (WINAPI *glGetIntegerv2_t)(GLenum, GLint *);
        static glGetFramebufferAttachmentParameteriv_t gfap;
        static glGetIntegerv2_t giv2;
        if (!gfap) gfap = (glGetFramebufferAttachmentParameteriv_t)trace_resolve("glGetFramebufferAttachmentParameteriv");
        if (!giv2) giv2 = (glGetIntegerv2_t)trace_resolve("glGetIntegerv");
        if (gfap) {
            GLint obj = -1, lvl = -1, texobj = -1;
            gfap(0x8D40, 0x8CE0, 0x8210, &obj);
            gfap(0x8D40, 0x8CE0, 0x8211, &lvl);
            if (giv2) giv2(0x8069 /* GL_TEXTURE_BINDING_2D */, &texobj);
            glog("DUMPNOW fb94-attach obj=%d lvl=%d texbind=%d u0=%u u1=%u\n",
                 obj, lvl, texobj, g_tex_unit0, g_tex_unit1);
        }
        dump_tex_image(0x0DE1, 20);
        dump_tex_image(0x0DE1, 99);
        dump_tex_image(0x0DE1, 110);
        dump_tex_image(0x0DE1, 111);
        dump_tex_image(0x0DE1, 118);
        dump_tex_image(0x0DE1, 120);
        dump_tex_image(0x0DE1, 96);
        dump_framebuffer_readback(0, "default_now");
        dump_framebuffer_readback(94, "fb94_now");
        glog("DUMPNOW done frame=%llu\n", (unsigned long long)g_frame_count);
        DeleteFileA("C:\\fgo\\_tools\\glshim\\dumpnow.on");
    }
    if (g_current_program == 563) {
        if (g_tex_unit0) g_tex563_u0 = g_tex_unit0;
        if (g_tex_unit1) g_tex563_u1 = g_tex_unit1;
        if (!g_tex563_dumped &&
            GetFileAttributesA("C:\\fgo\\_tools\\glshim\\dump563.on") != INVALID_FILE_ATTRIBUTES) {
            glog("TEX563 trigger dump frame=%llu u0=%u u1=%u\n",
                 (unsigned long long)g_frame_count, g_tex563_u0, g_tex563_u1);
            dump_tex_image(0x0DE1, 120);
            dump_tex_image(0x0DE1, 96);
            g_tex563_dumped = 1;
        }
        if (g_tex563_record_logged < 10) {
            glog("TEX563REC frame=%llu u0=%u u1=%u\n",
                 (unsigned long long)g_frame_count, g_tex_unit0, g_tex_unit1);
            g_tex563_record_logged++;
        }
    }
    if (g_current_program == 15 && !g_scene_dumped &&
        ((g_frame_count > 4000 && g_frame_count < 4040) ||
         (g_frame_count > 14000 && g_frame_count < 14040))) {
        dump_tex_image(0x0DE1, 99);
        dump_tex_image(0x0DE1, 101);
        dump_tex_image(0x0DE1, 110);
        dump_tex_image(0x0DE1, 120);
        dump_tex_image(0x0DE1, 96);
        dump_framebuffer_readback(96, "f96");
        dump_framebuffer_readback(0, "default");
        glog("FBO scene textures dumped at frame=%llu\n",
             (unsigned long long)g_frame_count);
        g_scene_dumped = 1;
    }
    int any_attr = 0;
    for (int i = 0; i < NV_MAX_ATTRIBS; i++) {
        if (g_nv_va[i].addr != 0) {
            any_attr = 1;
            break;
        }
    }
    if (g_draw_details < 400 || any_attr) {
        if (g_current_program == 15 && g_fbo_dumped < 8 &&
            (g_fbo_dumped == 0 || (g_frame_count % 1200) < 40)) {
            glog("FBO trigger prog15 tex_unit0=%u details=%d frame=%llu\n",
                 g_tex_unit0, g_draw_details, (unsigned long long)g_frame_count);
            dump_tex_image(0x0DE1, 99);
            g_fbo_dumped++;
        }
        if (mode == 0x4 && count == 3 && primcount == 1 && g_draw_details < 12) {
            /* dump the current vertex buffer content for fullscreen triangles */
            if (g_vbuf[0].set && g_vbuf[0].buffer != 0) {
                if (!real_glGetNamedBufferSubData)
                    real_glGetNamedBufferSubData = (glGetNamedBufferSubData_t)trace_resolve("glGetNamedBufferSubData");
                if (real_glGetNamedBufferSubData) {
                    float v[18];
                    memset(v, 0, sizeof v);
                    real_glGetNamedBufferSubData(g_vbuf[0].buffer, g_vbuf[0].offset, sizeof v, v);
                    glog("VBUF prog=%u buf=%u off=%lld stride=%d verts=",
                         g_current_program, g_vbuf[0].buffer, (long long)g_vbuf[0].offset, g_vbuf[0].stride);
                    for (int k = 0; k < 18; k++) glog("%s%.3f", k ? "," : "", v[k]);
                    glog("\n");
                    /* dump PerDraw (binding 0) and PerShadow (binding 1) UBOs */
                    for (int ub = 0; ub < 2; ub++) {
                        if (g_ubo_buffer[ub] != 0 && g_ubo_length[ub] > 0) {
                            float u[256];
                            memset(u, 0, sizeof u);
                            int want = (int)g_ubo_length[ub];
                            if (want > 1024) want = 1024;
                            real_glGetNamedBufferSubData(g_ubo_buffer[ub], g_ubo_offset[ub], want, u);
                            glog("UBO%d prog=%u buf=%u off=%lld len=%d: ", ub, g_current_program,
                                 g_ubo_buffer[ub], (long long)g_ubo_offset[ub], want);
                            for (int k = 0; k < want / 4; k++) glog("%s%.3f", k ? "," : "", u[k]);
                            glog("\n");
                        }
                    }
                }
            }
        }
        if (mode == 0x4 && count == 3 && (g_draw_details >= 12 || any_attr) &&
            g_perdraw_full < 24) {
            /* compact: compute NDC of vertex 0 using each slot's matrix */
            if (g_ubo_buffer[0] != 0 && g_ubo_length[0] >= 1024) {
                if (!real_glGetNamedBufferSubData)
                    real_glGetNamedBufferSubData = (glGetNamedBufferSubData_t)trace_resolve("glGetNamedBufferSubData");
                if (real_glGetNamedBufferSubData) {
                    float u[256];
                    memset(u, 0, sizeof u);
                    real_glGetNamedBufferSubData(g_ubo_buffer[0], g_ubo_offset[0], 1024, u);
                    glog("PERDRAW prog=%u ubuf=%u uoff=%lld s0v0=%.3f,%.3f s2m0=%.3f,%.3f,%.3f,%.3f\n",
                         g_current_program, g_ubo_buffer[0], (long long)g_ubo_offset[0],
                         u[0], u[1], u[64], u[65], u[66], u[67]);
                    glog("PERDRAW8 prog=%u:", g_current_program);
                    for (int s = 0; s < 8; s++) {
                        int base = s * 32;
                        glog(" s%dM0=[%.3f,%.3f,%.3f,%.3f]", s,
                             u[base], u[base + 1], u[base + 2], u[base + 3]);
                    }
                    glog("\n");
                    g_perdraw_full++;
                    if (g_vbuf[0].set && g_vbuf[0].buffer != 0) {
                        float vv[6];
                        memset(vv, 0, sizeof vv);
                        real_glGetNamedBufferSubData(g_vbuf[0].buffer, g_vbuf[0].offset, sizeof vv, vv);
                        glog("VBUF2 uoff=%lld v0=%.3f,%.3f,%.3f v1=%.3f,%.3f,%.3f\n",
                             (long long)g_ubo_offset[0], vv[0], vv[1], vv[2], vv[3], vv[4], vv[5]);
                    }
                    /* also dump the projection (PerShadow binding 1) */
                    if (g_ubo_buffer[1] != 0 && g_ubo_length[1] >= 64) {
                        float pu[16];
                        memset(pu, 0, sizeof pu);
                        real_glGetNamedBufferSubData(g_ubo_buffer[1], g_ubo_offset[1], 64, pu);
                        glog("PROJ prog=%u buf=%u off=%lld: %.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f\n",
                             g_current_program, g_ubo_buffer[1], (long long)g_ubo_offset[1],
                             pu[0], pu[1], pu[2], pu[3], pu[4], pu[5], pu[6], pu[7],
                             pu[8], pu[9], pu[10], pu[11], pu[12], pu[13], pu[14], pu[15]);
                    }
                }
            }
        }
        if (count > 1000 && g_perdraw_full < 24) {
            if (g_ubo_buffer[1] != 0 && g_ubo_length[1] >= 208 &&
                real_glGetNamedBufferSubData) {
                float cu[52];
                memset(cu, 0, sizeof cu);
                real_glGetNamedBufferSubData(g_ubo_buffer[1], g_ubo_offset[1], 208, cu);
                glog("CAMERA prog=%u buf=%u off=%lld:", g_current_program,
                     g_ubo_buffer[1], (long long)g_ubo_offset[1]);
                for (int q = 0; q < 52; q++) glog("%s%.4f", q ? "," : "", cu[q]);
                glog("\n");
                g_perdraw_full++;
            }
        }
        glog("DRAW %s prog=%u mode=0x%x first=%d count=%d prim=%d extra=%p uni=%d fb=%u fmt=",
             fn, g_current_program, mode, first, count, primcount, extra, g_unified_attrib_state,
             g_bound_draw_framebuffer);
        for (int i = 0; i < NV_MAX_ATTRIBS; i++) {
            if (g_nv_va[i].addr != 0 || g_fmt[i].set) {
                GLuint buf = 0, off = 0;
                if (g_nv_va[i].addr != 0) decode_fake_addr(g_nv_va[i].addr, &buf, &off);
                glog("a%d{addr=0x%llx b=%u o=0x%x l=%d f=%s%d/0x%x/s%d} ",
                     i, (unsigned long long)g_nv_va[i].addr, buf, off, g_nv_va[i].len,
                     g_fmt[i].is_int ? "i" : "", g_fmt[i].size, g_fmt[i].type, g_fmt[i].stride);
            }
        }
        glog("\n");
        g_draw_details++;
    } else {
        glog("DRAW %s prog=%u mode=0x%x first=%d count=%d prim=%d extra=%p\n",
             fn, g_current_program, mode, first, count, primcount, extra);
    }
}

static void bluefind_after_draw(void)
{
    if (!g_bluefind_on || g_bluefind_logged >= 400) return;
    if (g_bound_draw_framebuffer != 0) return;
    static glReadPixels_t rp;
    static glGetIntegerv_t giv;
    static void (WINAPI *rb)(GLenum);
    if (!rp) rp = (glReadPixels_t)trace_resolve("glReadPixels");
    if (!giv) giv = (glGetIntegerv_t)trace_resolve("glGetIntegerv");
    if (!rb) rb = (void (WINAPI *)(GLenum))trace_resolve("glReadBuffer");
    if (!rp || !giv) return;
    GLint vp[4] = {0};
    giv(0x0BA2 /* GL_VIEWPORT */, vp);
    int w = vp[2], h = vp[3];
    if (w <= 0 || h <= 0 || w > 8192 || h > 8192) return;
    enum { GW = 24, GH = 14 };
    unsigned char buf[GW * GH * 3];
    memset(buf, 0, sizeof(buf));
    GLint prev_read = 0;
    if (rb) {
        giv(0x0C02 /* GL_READ_BUFFER */, &prev_read);
        rb(0x0405 /* GL_BACK */);
    }
    rp(0, 0, GW, GH, 0x1907 /* GL_RGB */, 0x1401 /* GL_UNSIGNED_BYTE */, buf);
    if (rb && prev_read != 0x0404 && prev_read != 0x0405) rb((GLenum)prev_read);
    int blue = 0;
    for (int i = 0; i < GW * GH; i++) {
        unsigned char r = buf[i * 3 + 0];
        unsigned char g = buf[i * 3 + 1];
        unsigned char b = buf[i * 3 + 2];
        if (b > 80 && b > r + 40 && b > g + 40) blue++;
    }
    if (blue >= 3) {
        glog("BLUEHIT frame=%llu prog=%u vp=%dx%d blue=%d/%d\n",
             (unsigned long long)g_frame_count, g_current_program, w, h, blue, GW * GH);
        g_bluefind_logged++;
    }
}

static void bluefind_frame(void)
{
    if (!g_bluefind_on || g_bluefind_logged >= 200) return;
    static glReadPixels_t rp;
    static glGetIntegerv_t giv;
    static void (WINAPI *rb)(GLenum);
    if (!rp) rp = (glReadPixels_t)trace_resolve("glReadPixels");
    if (!giv) giv = (glGetIntegerv_t)trace_resolve("glGetIntegerv");
    if (!rb) rb = (void (WINAPI *)(GLenum))trace_resolve("glReadBuffer");
    if (!rp || !giv) return;
    GLint vp[4] = {0};
    giv(0x0BA2 /* GL_VIEWPORT */, vp);
    int fw = vp[2], fh = vp[3];
    if (fw <= 0 || fh <= 0 || fw > 8192 || fh > 8192) return;
    int rw = fw < 1280 ? fw : 1280;
    int rh = fh < 720 ? fh : 720;
    unsigned char *buf = (unsigned char *)HeapAlloc(GetProcessHeap(), 0, (size_t)rw * rh * 3);
    if (!buf) return;
    memset(buf, 0, (size_t)rw * rh * 3);
    GLint prev_read = 0;
    if (rb) {
        giv(0x0C02 /* GL_READ_BUFFER */, &prev_read);
        rb(0x0405 /* GL_BACK */);
    }
    rp(0, 0, rw, rh, 0x1907 /* GL_RGB */, 0x1401 /* GL_UNSIGNED_BYTE */, buf);
    if (rb && prev_read != 0x0404 && prev_read != 0x0405) rb((GLenum)prev_read);
    int blue = 0;
    for (int y = 0; y < rh; y += 8) {
        for (int x = 0; x < rw; x += 8) {
            const unsigned char *p = buf + ((size_t)y * rw + x) * 3;
            if (p[2] > 80 && p[2] > p[0] + 40 && p[2] > p[1] + 40) blue++;
        }
    }
    if (blue >= 4) {
        g_blue_saw_swap = 1;
        GLuint u0 = g_tex563_u0 ? g_tex563_u0 : 120;
        GLuint u1 = g_tex563_u1 ? g_tex563_u1 : 96;
        glog("TEX563 dump at blue frame=%llu unit0=%u unit1=%u\n",
             (unsigned long long)g_frame_count, u0, u1);
        dump_scene_ring("blue");
        dump_tex_image_tag(0x0DE1, u0, "blue563");
        dump_tex_image_tag(0x0DE1, u1, "blue563");
        char path[MAX_PATH];
        _snprintf(path, sizeof(path), "C:\\fgo\\_tools\\glshim\\blue_gl_%llu_%dx%d.bmp",
                  (unsigned long long)g_frame_count, rw, rh);
        save_bmp(path, rw, rh, buf);
        glog("BLUEFRAME frame=%llu read=%dx%d blue=%d saved=%s\n",
             (unsigned long long)g_frame_count, rw, rh, blue, path);
        g_bluefind_logged++;
    }
    HeapFree(GetProcessHeap(), 0, buf);
}

void WINAPI wrap_glDrawArrays(GLenum mode, GLint first, GLsizei count)
{
    static glDrawArrays_t real;
    if (!real) real = (glDrawArrays_t)trace_resolve("glDrawArrays");
    apply_unified_attribs();
    draw_log("glDrawArrays", mode, first, count, 1, NULL);
    if (real) real(mode, first, count);
    bluefind_after_draw();
}

static void WINAPI wrap_glDrawArraysInstanced(GLenum mode, GLint first, GLsizei count, GLsizei primcount)
{
    static glDrawArraysInstanced_t real;
    if (!real) real = (glDrawArraysInstanced_t)trace_resolve("glDrawArraysInstanced");
    apply_unified_attribs();
    draw_log("glDrawArraysInstanced", mode, first, count, primcount, NULL);
    if (real) real(mode, first, count, primcount);
    bluefind_after_draw();
}

void WINAPI wrap_glDrawElements(GLenum mode, GLsizei count, GLenum type, const void *indices)
{
    static glDrawElements_t real;
    if (!real) real = (glDrawElements_t)trace_resolve("glDrawElements");
    apply_unified_attribs();
    draw_log("glDrawElements", mode, 0, count, 1, indices);
    const void *adj = (g_nv_ea_valid && g_nv_ea_off != 0) ? (const char *)indices + g_nv_ea_off : indices;
    if (real) real(mode, count, type, adj);
}

static void WINAPI wrap_glDrawElementsInstanced(GLenum mode, GLsizei count, GLenum type, const void *indices, GLsizei primcount)
{
    static glDrawElementsInstanced_t real;
    if (!real) real = (glDrawElementsInstanced_t)trace_resolve("glDrawElementsInstanced");
    apply_unified_attribs();
    draw_log("glDrawElementsInstanced", mode, 0, count, primcount, indices);
    const void *adj = (g_nv_ea_valid && g_nv_ea_off != 0) ? (const char *)indices + g_nv_ea_off : indices;
    if (real) real(mode, count, type, adj, primcount);
}

static void WINAPI wrap_glDrawElementsInstancedBaseVertex(GLenum mode, GLsizei count, GLenum type, const void *indices, GLsizei primcount, GLint basevertex)
{
    static glDrawElementsInstancedBaseVertex_t real;
    if (!real) real = (glDrawElementsInstancedBaseVertex_t)trace_resolve("glDrawElementsInstancedBaseVertex");
    apply_unified_attribs();
    draw_log("glDrawElementsInstancedBaseVertex", mode, 0, count, primcount, indices);
    glog("DRAW   bv=%d\n", basevertex);
    const void *adj = (g_nv_ea_valid && g_nv_ea_off != 0) ? (const char *)indices + g_nv_ea_off : indices;
    if (real) real(mode, count, type, adj, primcount, basevertex);
}

static void WINAPI wrap_glDrawElementsBaseVertex(GLenum mode, GLsizei count, GLenum type, const void *indices, GLint basevertex)
{
    static glDrawElementsBaseVertex_t real;
    if (!real) real = (glDrawElementsBaseVertex_t)trace_resolve("glDrawElementsBaseVertex");
    apply_unified_attribs();
    draw_log("glDrawElementsBaseVertex", mode, 0, count, 1, indices);
    glog("DRAW   bv=%d\n", basevertex);
    const void *adj = (g_nv_ea_valid && g_nv_ea_off != 0) ? (const char *)indices + g_nv_ea_off : indices;
    if (real) real(mode, count, type, adj, basevertex);
}

static void WINAPI wrap_glMultiDrawElementsBaseVertex(GLenum mode, const GLsizei *count, GLenum type, const void *const *indices, GLsizei drawcount, const GLint *basevertex)
{
    static glMultiDrawElementsBaseVertex_t real;
    if (!real) real = (glMultiDrawElementsBaseVertex_t)trace_resolve("glMultiDrawElementsBaseVertex");
    apply_unified_attribs();
    ui_diag(mode, NULL, count, basevertex ? basevertex[0] : 0);
    draw_log("glMultiDrawElementsBaseVertex", mode, 0, count ? count[0] : 0, drawcount, indices);
    glog("DRAW   count=%d bv=%d\n", count ? count[0] : 0, basevertex ? basevertex[0] : 0);
    if (g_nv_ea_valid && g_nv_ea_off != 0 && drawcount > 0 && drawcount <= 64) {
        const void *adj[64];
        for (GLsizei k = 0; k < drawcount; k++)
            adj[k] = (const char *)indices[k] + g_nv_ea_off;
        if (real) real(mode, count, type, adj, drawcount, basevertex);
    } else {
        if (real) real(mode, count, type, indices, drawcount, basevertex);
    }
}

typedef void (WINAPI *glDrawArraysInstancedBaseInstance_t)(GLenum, GLint, GLsizei, GLsizei, GLuint);
typedef void (WINAPI *glDrawElementsInstancedBaseInstance_t)(GLenum, GLsizei, GLenum, const void *, GLsizei, GLuint);
typedef void (WINAPI *glDrawElementsInstancedBaseVertexBaseInstance_t)(GLenum, GLsizei, GLenum, const void *, GLsizei, GLint, GLuint);

static void WINAPI wrap_glDrawArraysInstancedBaseInstance(GLenum mode, GLint first, GLsizei count, GLsizei primcount, GLuint baseinstance)
{
    static glDrawArraysInstancedBaseInstance_t real;
    if (!real) real = (glDrawArraysInstancedBaseInstance_t)trace_resolve("glDrawArraysInstancedBaseInstance");
    apply_unified_attribs();
    draw_log("glDrawArraysInstancedBaseInstance", mode, first, count, primcount, NULL);
    glog("DRAW   bi=%u\n", baseinstance);
    if (real) real(mode, first, count, primcount, baseinstance);
}

static void WINAPI wrap_glDrawElementsInstancedBaseInstance(GLenum mode, GLsizei count, GLenum type, const void *indices, GLsizei primcount, GLuint baseinstance)
{
    static glDrawElementsInstancedBaseInstance_t real;
    if (!real) real = (glDrawElementsInstancedBaseInstance_t)trace_resolve("glDrawElementsInstancedBaseInstance");
    apply_unified_attribs();
    draw_log("glDrawElementsInstancedBaseInstance", mode, 0, count, primcount, indices);
    glog("DRAW   bi=%u\n", baseinstance);
    const void *adj = (g_nv_ea_valid && g_nv_ea_off != 0) ? (const char *)indices + g_nv_ea_off : indices;
    if (real) real(mode, count, type, adj, primcount, baseinstance);
}

static void WINAPI wrap_glDrawElementsInstancedBaseVertexBaseInstance(GLenum mode, GLsizei count, GLenum type, const void *indices, GLsizei primcount, GLint basevertex, GLuint baseinstance)
{
    static glDrawElementsInstancedBaseVertexBaseInstance_t real;
    if (!real) real = (glDrawElementsInstancedBaseVertexBaseInstance_t)trace_resolve("glDrawElementsInstancedBaseVertexBaseInstance");
    apply_unified_attribs();
    draw_log("glDrawElementsInstancedBaseVertexBaseInstance", mode, 0, count, primcount, indices);
    glog("DRAW   bv=%d bi=%u\n", basevertex, baseinstance);
    const void *adj = (g_nv_ea_valid && g_nv_ea_off != 0) ? (const char *)indices + g_nv_ea_off : indices;
    if (real) real(mode, count, type, adj, primcount, basevertex, baseinstance);
}

static void WINAPI wrap_glDrawRangeElements(GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type, const void *indices)
{
    static glDrawRangeElements_t real;
    if (!real) real = (glDrawRangeElements_t)trace_resolve("glDrawRangeElements");
    apply_unified_attribs();
    draw_log("glDrawRangeElements", mode, 0, count, 1, indices);
    glog("DRAW   start=%u end=%u type=0x%x\n", start, end, type);
    const void *adj = (g_nv_ea_valid && g_nv_ea_off != 0) ? (const char *)indices + g_nv_ea_off : indices;
    if (real) real(mode, start, end, count, type, adj);
}

static void ui_diag(int mode, const GLint *first, const GLsizei *count, int first_override)
{
    if (g_current_program != 200 && g_current_program != 203) return;
    static int ui_diag_count;
    static int ui_items_logged;
    int f0 = first_override >= 0 ? first_override : (first ? first[0] : 0);
    int c0 = count ? count[0] : 0;
    if (ui_diag_count < 8) {
        ui_diag_count++;
        FILE *f = fopen("C:\\fgo\\_tools\\glshim\\swap.log", "a");
        if (f) {
            fprintf(f, "[%llu] UI-DIAG prog=%u mode=0x%x first=%d count=%d drawcount=%d\n",
                    (unsigned long long)g_frame_count, g_current_program, mode, f0, c0, 1);
            for (int i = 0; i < NV_MAX_ATTRIBS; i++) {
                if (g_nv_va[i].addr != 0 || g_fmt[i].set || g_vbuf[i].set) {
                    GLuint b = 0, off = 0;
                    if (g_nv_va[i].addr != 0) decode_fake_addr(g_nv_va[i].addr, &b, &off);
                    fprintf(f, "  UI-ATTR[%d] nvaddr=0x%llx buf=%u off=0x%x len=%d fmt=%d/0x%x/s%d ro=%u vbuf={%d b=%u off=%lld stride=%d} abind=%d enabled=%d\n",
                            i, (unsigned long long)g_nv_va[i].addr, b, off, g_nv_va[i].len,
                            g_fmt[i].size, g_fmt[i].type, g_fmt[i].stride, g_fmt[i].relativeoffset,
                            g_vbuf[i].set, g_vbuf[i].buffer, (long long)g_vbuf[i].offset,
                            g_vbuf[i].stride, g_abind[i].set ? (int)g_abind[i].binding : -1,
                            (g_enabled_mask >> i) & 1u);
                }
            }
            fprintf(f, "  UI-UBO0 buf=%u off=%lld len=%lld\n",
                    g_ubo_buffer[0], (long long)g_ubo_offset[0], (long long)g_ubo_length[0]);
            if (g_nv_va[0].addr != 0 && g_nv_va[0].len >= 192) {
                GLuint vbuf = 0, voff = 0;
                decode_fake_addr(g_nv_va[0].addr, &vbuf, &voff);
                if (!real_glGetNamedBufferSubData)
                    real_glGetNamedBufferSubData = (glGetNamedBufferSubData_t)trace_resolve("glGetNamedBufferSubData");
                if (real_glGetNamedBufferSubData && vbuf != 0) {
                    int bind = g_abind[0].set ? (int)g_abind[0].binding : 0;
                    int vstride = (bind >= 0 && bind < NV_MAX_ATTRIBS && g_vbuf[bind].set)
                                      ? g_vbuf[bind].stride : 16;
                    if (vstride <= 0) vstride = 16;
                    unsigned char raw[192];
                    memset(raw, 0, sizeof raw);
                    GLintptr dstart = (GLintptr)voff + (GLintptr)f0 * vstride;
                    real_glGetNamedBufferSubData(vbuf, dstart, sizeof raw, raw);
                    fprintf(f, "  UI-VTX prog=%u buf=%u off=0x%x vstride=%d first=%d count=%d\n",
                            g_current_program, vbuf, voff, vstride, f0, c0);
                    for (int v = 0; v < 6; v++) {
                        const float *p = (const float *)(raw + v * vstride);
                        fprintf(f, "    v%d pos=(%.2f,%.2f,%.2f) col=(%u,%u,%u,%u) raw=%02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x\n",
                                v, p[0], p[1], p[2],
                                raw[v*vstride+12], raw[v*vstride+13], raw[v*vstride+14], raw[v*vstride+15],
                                raw[v*vstride+0], raw[v*vstride+1], raw[v*vstride+2], raw[v*vstride+3],
                                raw[v*vstride+4], raw[v*vstride+5], raw[v*vstride+6], raw[v*vstride+7],
                                raw[v*vstride+8], raw[v*vstride+9], raw[v*vstride+10], raw[v*vstride+11]);
                    }
                }
            }
            if (g_ubo_buffer[0] != 0 && g_ubo_length[0] >= 64) {
                if (!real_glGetNamedBufferSubData)
                    real_glGetNamedBufferSubData = (glGetNamedBufferSubData_t)trace_resolve("glGetNamedBufferSubData");
                if (real_glGetNamedBufferSubData) {
                    float m[16];
                    memset(m, 0, sizeof m);
                    real_glGetNamedBufferSubData(g_ubo_buffer[0], g_ubo_offset[0], 64, m);
                    fprintf(f, "  UI-MAT %.4f,%.4f,%.4f,%.4f | %.4f,%.4f,%.4f,%.4f | %.4f,%.4f,%.4f,%.4f | %.4f,%.4f,%.4f,%.4f\n",
                            m[0],m[1],m[2],m[3],m[4],m[5],m[6],m[7],m[8],m[9],m[10],m[11],m[12],m[13],m[14],m[15]);
                }
            }
            fclose(f);
        }
    }
    if (ui_items_logged < 400) {
        ui_items_logged++;
        FILE *f = fopen("C:\\fgo\\_tools\\glshim\\ui_items.log", "a");
        if (f) {
            GLuint b0 = 0; GLuint o0 = 0;
            if (g_nv_va[0].addr) decode_fake_addr(g_nv_va[0].addr, &b0, &o0);
            fprintf(f, "frame=%llu prog=%u first=%d count=%d buf=%u off=0x%x mode=0x%x\n",
                    (unsigned long long)g_frame_count, g_current_program, f0, c0, b0, o0, mode);
            fclose(f);
        }
    }
}

static void WINAPI wrap_glMultiDrawArrays(GLenum mode, const GLint *first, const GLsizei *count, GLsizei drawcount)
{
    static glMultiDrawArrays_t real;
    if (!real) real = (glMultiDrawArrays_t)trace_resolve("glMultiDrawArrays");
    apply_unified_attribs();
    ui_diag(mode, first, count, -1);
    glog("DRAW glMultiDrawArrays prog=%u mode=0x%x first=%p count=%p drawcount=%d\n",
         g_current_program, mode, first, count, drawcount);
    if (real) real(mode, first, count, drawcount);
    if (g_current_program == 200) {
        typedef void (WINAPI *glReadPixels_t)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *);
        typedef void (WINAPI *glReadBuffer_t)(GLenum);
        typedef void (WINAPI *glGetIntegerv_t)(GLenum, GLint *);
        static glReadPixels_t rp;
        static glReadBuffer_t rb;
        static glGetIntegerv_t giv;
        if (!rp) rp = (glReadPixels_t)trace_resolve("glReadPixels");
        if (!rb) rb = (glReadBuffer_t)trace_resolve("glReadBuffer");
        if (!giv) giv = (glGetIntegerv_t)trace_resolve("glGetIntegerv");
        if (rp && rb && giv) {
            enum { PW = 128, PH = 72 };
            unsigned char buf[PW * PH * 3];
            memset(buf, 0, sizeof buf);
            GLint prev = 0;
            giv(0x0C02, &prev);
            rb(0x0404 /* GL_FRONT */);
            rp(0, 0, PW, PH, 0x1907, 0x1401, buf);
            if (prev != 0x0404 && prev != 0x0405 && prev != 0) rb((GLenum)prev);
            long long sum[3] = {0,0,0};
            int nonzero = 0, white = 0;
            for (int i = 0; i < PW * PH; i++) {
                unsigned char r = buf[i*3+0], g = buf[i*3+1], b = buf[i*3+2];
                sum[0] += r; sum[1] += g; sum[2] += b;
                if (r || g || b) nonzero++;
                if (r > 235 && g > 235 && b > 235) white++;
            }
            FILE *f = fopen("C:\\fgo\\_tools\\glshim\\swap.log", "a");
            if (f) {
                fprintf(f, "[%llu] FRONT-AFTER-UI avg=(%lld,%lld,%lld) nonzero=%d/%d white=%d\n",
                        (unsigned long long)g_frame_count,
                        sum[0]/(PW*PH), sum[1]/(PW*PH), sum[2]/(PW*PH),
                        nonzero, PW*PH, white);
                fclose(f);
            }
        }
    }
}

static void WINAPI wrap_glMultiDrawElements(GLenum mode, const GLsizei *count, GLenum type, const void *const *indices, GLsizei drawcount)
{
    static glMultiDrawElements_t real;
    if (!real) real = (glMultiDrawElements_t)trace_resolve("glMultiDrawElements");
    apply_unified_attribs();
    glog("DRAW glMultiDrawElements prog=%u mode=0x%x count=%p type=0x%x idx=%p drawcount=%d\n",
         g_current_program, mode, count, type, indices, drawcount);
    if (g_nv_ea_valid && g_nv_ea_off != 0 && drawcount > 0 && drawcount <= 64) {
        const void *adj[64];
        for (GLsizei k = 0; k < drawcount; k++)
            adj[k] = (const char *)indices[k] + g_nv_ea_off;
        if (real) real(mode, count, type, adj, drawcount);
    } else {
        if (real) real(mode, count, type, indices, drawcount);
    }
}

static void WINAPI wrap_glDrawArraysIndirect(GLenum mode, const void *indirect)
{
    static glDrawArraysIndirect_t real;
    if (!real) real = (glDrawArraysIndirect_t)trace_resolve("glDrawArraysIndirect");
    apply_unified_attribs();
    glog("DRAW glDrawArraysIndirect prog=%u mode=0x%x indirect=%p\n", g_current_program, mode, indirect);
    if (real) real(mode, indirect);
}

static void WINAPI wrap_glDrawElementsIndirect(GLenum mode, GLenum type, const void *indirect)
{
    static glDrawElementsIndirect_t real;
    if (!real) real = (glDrawElementsIndirect_t)trace_resolve("glDrawElementsIndirect");
    apply_unified_attribs();
    glog("DRAW glDrawElementsIndirect prog=%u mode=0x%x type=0x%x indirect=%p\n", g_current_program, mode, type, indirect);
    if (real) real(mode, type, indirect);
}

static void WINAPI wrap_glMultiDrawArraysIndirect(GLenum mode, const void *indirect, GLsizei drawcount, GLsizei stride)
{
    static glMultiDrawArraysIndirect_t real;
    if (!real) real = (glMultiDrawArraysIndirect_t)trace_resolve("glMultiDrawArraysIndirect");
    apply_unified_attribs();
    glog("DRAW glMultiDrawArraysIndirect prog=%u mode=0x%x indirect=%p drawcount=%d stride=%d\n",
         g_current_program, mode, indirect, drawcount, stride);
    if (real) real(mode, indirect, drawcount, stride);
}

static void WINAPI wrap_glMultiDrawElementsIndirect(GLenum mode, GLenum type, const void *indirect, GLsizei drawcount, GLsizei stride)
{
    static glMultiDrawElementsIndirect_t real;
    if (!real) real = (glMultiDrawElementsIndirect_t)trace_resolve("glMultiDrawElementsIndirect");
    apply_unified_attribs();
    glog("DRAW glMultiDrawElementsIndirect prog=%u mode=0x%x type=0x%x indirect=%p drawcount=%d stride=%d\n",
         g_current_program, mode, type, indirect, drawcount, stride);
    if (real) real(mode, type, indirect, drawcount, stride);
}

static void WINAPI wrap_glMultiDrawArraysIndirectCount(GLenum mode, const void *indirect, GLintptr drawcount, GLsizei maxdrawcount, GLsizei stride)
{
    static glMultiDrawArraysIndirectCount_t real;
    if (!real) real = (glMultiDrawArraysIndirectCount_t)trace_resolve("glMultiDrawArraysIndirectCount");
    apply_unified_attribs();
    glog("DRAW glMultiDrawArraysIndirectCount prog=%u mode=0x%x indirect=%p drawcount=%lld max=%d stride=%d\n",
         g_current_program, mode, indirect, (long long)drawcount, maxdrawcount, stride);
    if (real) real(mode, indirect, drawcount, maxdrawcount, stride);
}

static void WINAPI wrap_glMultiDrawElementsIndirectCount(GLenum mode, GLenum type, const void *indirect, GLintptr drawcount, GLsizei maxdrawcount, GLsizei stride)
{
    static glMultiDrawElementsIndirectCount_t real;
    if (!real) real = (glMultiDrawElementsIndirectCount_t)trace_resolve("glMultiDrawElementsIndirectCount");
    apply_unified_attribs();
    glog("DRAW glMultiDrawElementsIndirectCount prog=%u mode=0x%x type=0x%x indirect=%p drawcount=%lld max=%d stride=%d\n",
         g_current_program, mode, type, indirect, (long long)drawcount, maxdrawcount, stride);
    if (real) real(mode, type, indirect, drawcount, maxdrawcount, stride);
}

static void WINAPI wrap_glGenBuffers(GLsizei n, GLuint *buffers)
{
    static glGenBuffers_t real;
    if (!real) real = (glGenBuffers_t)trace_resolve("glGenBuffers");
    glog("BUF glGenBuffers n=%d\n", n);
    if (real) real(n, buffers);
}

static void WINAPI wrap_glDeleteBuffers(GLsizei n, const GLuint *buffers)
{
    static glDeleteBuffers_t real;
    if (!real) real = (glDeleteBuffers_t)trace_resolve("glDeleteBuffers");
    if (n > 0 && buffers) glog("BUF glDeleteBuffers n=%d first=%u\n", n, buffers[0]);
    else glog("BUF glDeleteBuffers n=%d\n", n);
    if (real) real(n, buffers);
}

static int g_captures_written;

static void WINAPI wrap_glBindFramebuffer(GLenum target, GLuint fb)
{
    static glBindFramebuffer_t real;
    if (!real) real = (glBindFramebuffer_t)trace_resolve("glBindFramebuffer");
    if (fb >= 90)
        keytex_log("glBindFramebuffer target=0x%x fb=%u", target, fb);
    if (fb == 0xFFFFFFFFu) {
        /* The engine uses -1 as the default framebuffer sentinel.  The AMD ICD
           rejects the invalid name at swap time, so translate it to real 0. */
        glog("STATE glBindFramebuffer target=0x%x fb=-1 -> 0\n", target);
        fb = 0;
    }
    if (target == 0x8D40 /* GL_FRAMEBUFFER */ || target == 0x8CA9 /* GL_DRAW_FRAMEBUFFER */) {
        g_bound_draw_framebuffer = fb;
    }
    if (target == 0x8D40 || target == 0x8CAA /* GL_READ_FRAMEBUFFER */) {
        g_bound_read_framebuffer = fb;
    }
    if (fb == 0) glog("STATE glBindFramebuffer target=0x%x fb=0\n", target);
    if (real) real(target, fb);
}

static void WINAPI wrap_glViewport(GLint x, GLint y, GLsizei w, GLsizei h)
{
    static glViewport_t real;
    if (!real) real = (glViewport_t)trace_resolve("glViewport");
    glog("STATE glViewport %d,%d %dx%d\n", x, y, w, h);
    if (real) real(x, y, w, h);
}

static void WINAPI wrap_glBlitFramebuffer(GLint sx0, GLint sy0, GLint sx1, GLint sy1,
                                          GLint dx0, GLint dy0, GLint dx1, GLint dy1,
                                          GLbitfield mask, GLenum filter)
{
    static glBlitFramebuffer_t real;
    if (!real) real = (glBlitFramebuffer_t)trace_resolve("glBlitFramebuffer");
    if (real) real(sx0, sy0, sx1, sy1, dx0, dy0, dx1, dy1, mask, filter);
}

void WINAPI wrap_glClear(GLbitfield mask)
{
    static glClear_t real;
    if (!real) real = (glClear_t)GetProcAddress(g_real, "glClear");
    glog("STATE glClear mask=0x%x frame=%llu\n", mask, (unsigned long long)g_frame_count);
    state_log("glClear mask=0x%x fb=%u prog=%u", mask, g_bound_draw_framebuffer, g_current_program);
    if (real) real(mask);
}

typedef void (WINAPI *glColorMask_t)(GLboolean, GLboolean, GLboolean, GLboolean);
typedef void (WINAPI *glDepthMask_t)(GLboolean);
typedef void (WINAPI *glDrawBuffers_t)(GLsizei, const GLenum *);
typedef void (WINAPI *glEnable_t)(GLenum);
typedef void (WINAPI *glDisable_t)(GLenum);
typedef GLenum (WINAPI *glCheckFramebufferStatus_t)(GLenum);

static void WINAPI wrap_glColorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a)
{
    static glColorMask_t real;
    if (!real) real = (glColorMask_t)trace_resolve("glColorMask");
    state_log("glColorMask %d,%d,%d,%d fb=%u prog=%u",
              (int)r, (int)g, (int)b, (int)a, g_bound_draw_framebuffer, g_current_program);
    if (real) real(r, g, b, a);
}

static void WINAPI wrap_glDepthMask(GLboolean flag)
{
    static glDepthMask_t real;
    if (!real) real = (glDepthMask_t)trace_resolve("glDepthMask");
    state_log("glDepthMask %d fb=%u prog=%u", (int)flag, g_bound_draw_framebuffer, g_current_program);
    if (real) real(flag);
}

static void WINAPI wrap_glDrawBuffers(GLsizei n, const GLenum *bufs)
{
    static glDrawBuffers_t real;
    if (!real) real = (glDrawBuffers_t)trace_resolve("glDrawBuffers");
    char b[256];
    int off = 0;
    for (GLsizei i = 0; i < n && off < 240; i++) {
        off += _snprintf(b + off, sizeof(b) - (size_t)off, "%s0x%x", i ? "," : "", bufs ? bufs[i] : 0);
    }
    state_log("glDrawBuffers n=%d [%s] fb=%u prog=%u", (int)n, b, g_bound_draw_framebuffer, g_current_program);
    if (real) real(n, bufs);
}

static void WINAPI wrap_glEnable(GLenum cap)
{
    static glEnable_t real;
    if (!real) real = (glEnable_t)trace_resolve("glEnable");
    state_log("glEnable cap=0x%x fb=%u prog=%u", cap, g_bound_draw_framebuffer, g_current_program);
    if (real) real(cap);
}

static void WINAPI wrap_glDisable(GLenum cap)
{
    static glDisable_t real;
    if (!real) real = (glDisable_t)trace_resolve("glDisable");
    state_log("glDisable cap=0x%x fb=%u prog=%u", cap, g_bound_draw_framebuffer, g_current_program);
    if (real) real(cap);
}

static void WINAPI wrap_glAttachShader(GLuint program, GLuint shader);
static void WINAPI wrap_glLinkProgram(GLuint program);

static PROC find_hook(const char *name)
{
    if (strcmp(name, "glUseProgram") == 0) return (PROC)wrap_glUseProgram;
    if (strcmp(name, "glBindVertexArray") == 0) return (PROC)wrap_glBindVertexArray;
    if (strcmp(name, "glBindBuffer") == 0) return (PROC)wrap_glBindBuffer;
    if (strcmp(name, "glBindBufferBase") == 0) return (PROC)wrap_glBindBufferBase;
    if (strcmp(name, "glBindBufferRange") == 0) return (PROC)wrap_glBindBufferRange;
    if (strcmp(name, "glBufferData") == 0) return (PROC)wrap_glBufferData;
    if (strcmp(name, "glBufferSubData") == 0) return (PROC)wrap_glBufferSubData;
    if (strcmp(name, "glNamedBufferData") == 0) return (PROC)wrap_glNamedBufferData;
    if (strcmp(name, "glNamedBufferSubData") == 0) return (PROC)wrap_glNamedBufferSubData;
    if (strcmp(name, "glBufferStorage") == 0) return (PROC)wrap_glBufferStorage;
    if (strcmp(name, "glNamedBufferStorage") == 0) return (PROC)wrap_glNamedBufferStorage;
    if (strcmp(name, "glMapBuffer") == 0) return (PROC)wrap_glMapBuffer;
    if (strcmp(name, "glMapBufferRange") == 0) return (PROC)wrap_glMapBufferRange;
    if (strcmp(name, "glMapNamedBuffer") == 0) return (PROC)wrap_glMapNamedBuffer;
    if (strcmp(name, "glMapNamedBufferRange") == 0) return (PROC)wrap_glMapNamedBufferRange;
    if (strcmp(name, "glVertexAttribPointer") == 0) return (PROC)wrap_glVertexAttribPointer;
    if (strcmp(name, "glVertexAttribIPointer") == 0) return (PROC)wrap_glVertexAttribIPointer;
    if (strcmp(name, "glVertexAttribFormat") == 0) return (PROC)wrap_glVertexAttribFormat;
    if (strcmp(name, "glVertexAttribIFormat") == 0) return (PROC)wrap_glVertexAttribIFormat;
    if (strcmp(name, "glVertexAttribLFormat") == 0) return (PROC)wrap_glVertexAttribLFormat;
    if (strcmp(name, "glVertexAttribBinding") == 0) return (PROC)wrap_glVertexAttribBinding;
    if (strcmp(name, "glBindVertexBuffer") == 0) return (PROC)wrap_glBindVertexBuffer;
    if (strcmp(name, "glEnableVertexAttribArray") == 0) return (PROC)wrap_glEnableVertexAttribArray;
    if (strcmp(name, "glDisableVertexAttribArray") == 0) return (PROC)wrap_glDisableVertexAttribArray;
    if (strcmp(name, "glVertexArrayAttribFormat") == 0) return (PROC)wrap_glVertexArrayAttribFormat;
    if (strcmp(name, "glVertexArrayAttribIFormat") == 0) return (PROC)wrap_glVertexArrayAttribIFormat;
    if (strcmp(name, "glVertexArrayAttribLFormat") == 0) return (PROC)wrap_glVertexArrayAttribLFormat;
    if (strcmp(name, "glVertexArrayAttribBinding") == 0) return (PROC)wrap_glVertexArrayAttribBinding;
    if (strcmp(name, "glVertexArrayVertexBuffer") == 0) return (PROC)wrap_glVertexArrayVertexBuffer;
    if (strcmp(name, "glEnableVertexArrayAttrib") == 0) return (PROC)wrap_glEnableVertexArrayAttrib;
    if (strcmp(name, "glDisableVertexArrayAttrib") == 0) return (PROC)wrap_glDisableVertexArrayAttrib;
    if (strcmp(name, "glEnableClientState") == 0) return (PROC)wrap_glEnableClientState;
    if (strcmp(name, "glDisableClientState") == 0) return (PROC)wrap_glDisableClientState;
    if (strcmp(name, "glAttachShader") == 0) return (PROC)wrap_glAttachShader;
    if (strcmp(name, "glLinkProgram") == 0) return (PROC)wrap_glLinkProgram;
    if (strcmp(name, "glDrawArrays") == 0) return (PROC)wrap_glDrawArrays;
    if (strcmp(name, "glDrawArraysInstanced") == 0) return (PROC)wrap_glDrawArraysInstanced;
    if (strcmp(name, "glDrawElements") == 0) return (PROC)wrap_glDrawElements;
    if (strcmp(name, "glDrawElementsInstanced") == 0) return (PROC)wrap_glDrawElementsInstanced;
    if (strcmp(name, "glDrawElementsInstancedBaseVertex") == 0) return (PROC)wrap_glDrawElementsInstancedBaseVertex;
    if (strcmp(name, "glDrawElementsBaseVertex") == 0) return (PROC)wrap_glDrawElementsBaseVertex;
    if (strcmp(name, "glMultiDrawElementsBaseVertex") == 0) return (PROC)wrap_glMultiDrawElementsBaseVertex;
    if (strcmp(name, "glDrawArraysInstancedBaseInstance") == 0) return (PROC)wrap_glDrawArraysInstancedBaseInstance;
    if (strcmp(name, "glDrawElementsInstancedBaseInstance") == 0) return (PROC)wrap_glDrawElementsInstancedBaseInstance;
    if (strcmp(name, "glDrawElementsInstancedBaseVertexBaseInstance") == 0) return (PROC)wrap_glDrawElementsInstancedBaseVertexBaseInstance;
    if (strcmp(name, "glDrawRangeElements") == 0) return (PROC)wrap_glDrawRangeElements;
    /* glMultiDrawArrays wrap restored: the old 16:21 shim wrapped this call
       and ran apply_unified_attribs before every multi-draw (verified by
       disassembling opengl32.dll.1621).  VARIANT E removed it on a wrong
       assumption and the UI pass stopped producing output. */
    if (strcmp(name, "glMultiDrawArrays") == 0) return (PROC)wrap_glMultiDrawArrays;
    if (strcmp(name, "glMultiDrawElements") == 0) return (PROC)wrap_glMultiDrawElements;
    if (strcmp(name, "glDrawArraysIndirect") == 0) return (PROC)wrap_glDrawArraysIndirect;
    if (strcmp(name, "glDrawElementsIndirect") == 0) return (PROC)wrap_glDrawElementsIndirect;
    if (strcmp(name, "glMultiDrawArraysIndirect") == 0) return (PROC)wrap_glMultiDrawArraysIndirect;
    if (strcmp(name, "glMultiDrawElementsIndirect") == 0) return (PROC)wrap_glMultiDrawElementsIndirect;
    if (strcmp(name, "glMultiDrawArraysIndirectCount") == 0) return (PROC)wrap_glMultiDrawArraysIndirectCount;
    if (strcmp(name, "glMultiDrawElementsIndirectCount") == 0) return (PROC)wrap_glMultiDrawElementsIndirectCount;
    if (strcmp(name, "glGenBuffers") == 0) return (PROC)wrap_glGenBuffers;
    if (strcmp(name, "glDeleteBuffers") == 0) return (PROC)wrap_glDeleteBuffers;
    if (strcmp(name, "glViewport") == 0) return (PROC)wrap_glViewport;
    if (strcmp(name, "glClear") == 0) return (PROC)wrap_glClear;
    if (strcmp(name, "glColorMask") == 0) return (PROC)wrap_glColorMask;
    if (strcmp(name, "glDepthMask") == 0) return (PROC)wrap_glDepthMask;
    if (strcmp(name, "glDrawBuffers") == 0) return (PROC)wrap_glDrawBuffers;
    if (strcmp(name, "glEnable") == 0) return (PROC)wrap_glEnable;
    if (strcmp(name, "glDisable") == 0) return (PROC)wrap_glDisable;
    if (strcmp(name, "glBindFramebuffer") == 0) return (PROC)wrap_glBindFramebuffer;
    if (strcmp(name, "glFramebufferTexture2D") == 0) return (PROC)wrap_glFramebufferTexture2D;
    if (strcmp(name, "glFramebufferTexture2DEXT") == 0) return (PROC)wrap_glFramebufferTexture2D;
    if (strcmp(name, "glTextureStorage2D") == 0) return (PROC)wrap_glTextureStorage2D;
    if (strcmp(name, "glTextureSubImage2D") == 0) return (PROC)wrap_glTextureSubImage2D;
    if (strcmp(name, "glCompressedTextureSubImage2D") == 0) return (PROC)wrap_glCompressedTextureSubImage2D;
    if (strcmp(name, "glBindImageTexture") == 0) return (PROC)wrap_glBindImageTexture;
    if (strcmp(name, "glTexStorage2D") == 0) return (PROC)wrap_glTexStorage2D;
    if (strcmp(name, "glTexSubImage2D") == 0) return (PROC)wrap_glTexSubImage2D;
    if (strcmp(name, "glGetError") == 0) return (PROC)wrap_glGetError;
    if (strcmp(name, "glCopyImageSubData") == 0) return (PROC)wrap_glCopyImageSubData;
    if (strcmp(name, "glCopyTextureSubImage2D") == 0) return (PROC)wrap_glCopyTextureSubImage2D;
    if (strcmp(name, "glNamedFramebufferTexture") == 0) return (PROC)wrap_glNamedFramebufferTexture;
    if (strcmp(name, "glBlitFramebuffer") == 0) return (PROC)wrap_glBlitFramebuffer;
    if (strcmp(name, "glUniform4fv") == 0) return (PROC)wrap_glUniform4fv;
    if (strcmp(name, "glUniform4f") == 0) return (PROC)wrap_glUniform4f;
    if (strcmp(name, "glActiveTexture") == 0) return (PROC)wrap_glActiveTexture;
    if (strcmp(name, "glBindTexture") == 0) return (PROC)wrap_glBindTexture;
    if (strcmp(name, "glBindTextures") == 0) return (PROC)wrap_glBindTextures;
    if (strcmp(name, "glBindTextureUnit") == 0) return (PROC)wrap_glBindTextureUnit;
    if (strcmp(name, "glBindMultiTextureEXT") == 0) return (PROC)wrap_glBindMultiTextureEXT;
    return NULL;
}

static PROC find_stub(const char *name)
{
    if (strcmp(name, "glMakeNamedBufferResidentNV") == 0) return (PROC)stub_glMakeNamedBufferResidentNV;
    if (strcmp(name, "glMakeNamedBufferNonResidentNV") == 0) return (PROC)stub_glMakeNamedBufferNonResidentNV;
    if (strcmp(name, "glGetNamedBufferParameterui64vNV") == 0) return (PROC)stub_glGetNamedBufferParameterui64vNV;
    if (strcmp(name, "glGetBufferParameterui64vNV") == 0) return (PROC)stub_glGetBufferParameterui64vNV;
    if (strcmp(name, "glGetIntegerui64vNV") == 0) return (PROC)stub_glGetIntegerui64vNV;
    if (strcmp(name, "glVertexAttribFormatNV") == 0) return (PROC)stub_glVertexAttribFormatNV;
    if (strcmp(name, "glVertexAttribIFormatNV") == 0) return (PROC)stub_glVertexAttribIFormatNV;
    if (strcmp(name, "glVertexAttribLFormatNV") == 0) return (PROC)stub_glVertexAttribLFormatNV;
    if (strcmp(name, "glBufferAddressRangeNV") == 0) return (PROC)stub_glBufferAddressRangeNV;
    if (strcmp(name, "glGetIntegerui64i_vNV") == 0) return (PROC)stub_glGetIntegerui64i_vNV;
    if (strcmp(name, "glMultiDrawArraysIndirectBindlessNV") == 0) return (PROC)stub_glMultiDrawArraysIndirectBindlessNV;
    if (strcmp(name, "glMultiDrawElementsIndirectBindlessNV") == 0) return (PROC)stub_glMultiDrawElementsIndirectBindlessNV;
    return NULL;
}

static HMODULE load_real(void)
{
    wchar_t path[MAX_PATH];
    UINT n = GetSystemDirectoryW(path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH - 16) return NULL;
    wcscat(path, L"\\opengl32.dll");
    return LoadLibraryW(path);
}


/* ---------- glShaderSource shader-fix shim ---------- */

typedef void (WINAPI *glShaderSource_t)(unsigned int, int, const char *const *, const int *);
typedef unsigned int (WINAPI *glCreateShader_t)(GLenum);
typedef void (WINAPI *glGetShaderiv_t)(GLuint, GLenum, GLint *);
typedef void (WINAPI *glGetShaderInfoLog_t)(GLuint, GLsizei, GLsizei *, char *);
typedef void (WINAPI *glGetProgramiv_t)(GLuint, GLenum, GLint *);
typedef void (WINAPI *glGetProgramInfoLog_t)(GLuint, GLsizei, GLsizei *, char *);
typedef void (WINAPI *glDeleteShader_t)(GLuint);
typedef void (WINAPI *glAttachShader_t)(GLuint, GLuint);
typedef void (WINAPI *glLinkProgram_t)(GLuint);
typedef void (WINAPI *glCompileShader_t)(GLuint);
static glShaderSource_t real_glShaderSource;
static glCreateShader_t real_glCreateShader;
static GLenum g_shader_types[65536];
static glGetShaderiv_t real_glGetShaderiv;
static glGetShaderInfoLog_t real_glGetShaderInfoLog;
static glGetProgramiv_t real_glGetProgramiv;
static glGetProgramInfoLog_t real_glGetProgramInfoLog;
static glDeleteShader_t real_glDeleteShader;
static struct { char *src; int len; } g_shader_src[65536];
static GLuint g_prog_shaders[65536][8];
static int g_prog_shader_count[65536];
static int g_shader_replaced[65536];
static glAttachShader_t real_glAttachShader;
static glLinkProgram_t real_glLinkProgram;
static glCompileShader_t real_glCompileShader;
static int contains_n(const char *s, int len, const char *needle);
static void store_shader_src(GLuint id, const char *src, int len);

/* Find the distinct `vecN texcoord;` types inside VertexData blocks.
   Returns 2/3/4 if exactly one distinct type exists, else 0. */
static int shader_texcoord_type(const char *src, int len)
{
    int types[5] = {0};
    for (int i = 0; i + 10 < len; i++) {
        if (memcmp(src + i, "VertexData", 10) != 0) continue;
        int j = i + 10;
        while (j < len && src[j] != '{') j++;
        if (j >= len) continue;
        int k = j + 1;
        while (k < len && src[k] != '}') k++;
        if (k >= len) continue;
        for (int q = j + 1; q + 13 <= k; q++) {
            if (memcmp(src + q, "vec", 3) != 0) continue;
            int d = src[q + 3];
            if (d != '2' && d != '3' && d != '4') continue;
            int r = q + 4;
            while (r < k && (src[r] == ' ' || src[r] == '\t')) r++;
            if (r + 8 <= k && memcmp(src + r, "texcoord", 8) == 0) {
                int e = r + 8;
                while (e < k && (src[e] == ' ' || src[e] == '\t')) e++;
                if (e < k && src[e] == ';') types[d - '0'] = 1;
            }
        }
    }
    int found = 0, ret = 0;
    for (int t = 2; t <= 4; t++) {
        if (types[t]) { found++; ret = t; }
    }
    return found == 1 ? ret : 0;
}

static int uses_texcoord_zw(const char *src, int len)
{
    return contains_n(src, len, "texcoord.zw") ||
           contains_n(src, len, "texcoord . zw") ||
           contains_n(src, len, "texcoord .zw");
}

static int patch_texcoord_type(const char *src, int len, char *out, int cap, int newtype)
{
    char *p = out;
    char *end = out + cap - 1;
    int i = 0;
    int patched = 0;
    while (i < len && p < end) {
        if (!patched && i + 3 < len && memcmp(src + i, "vec", 3) == 0) {
            int d = src[i + 3];
            if (d == '2' || d == '3' || d == '4') {
                int r = i + 4;
                while (r < len && (src[r] == ' ' || src[r] == '\t')) r++;
                if (r + 8 <= len && memcmp(src + r, "texcoord", 8) == 0) {
                    int e = r + 8;
                    while (e < len && (src[e] == ' ' || src[e] == '\t')) e++;
                    if (e < len && src[e] == ';') {
                        /* check we're inside a VertexData block */
                        int inside = 0;
                        for (int q = i; q >= 0; q--) {
                            if (src[q] == '}') break;
                            if (q + 10 <= i && memcmp(src + q, "VertexData", 10) == 0) { inside = 1; break; }
                        }
                        if (inside) {
                            *p++ = 'v';
                            *p++ = 'e';
                            *p++ = 'c';
                            *p++ = (char)('0' + newtype);
                            i += 4;
                            patched = 1;
                            continue;
                        }
                    }
                }
            }
        }
        *p++ = src[i++];
    }
    if (p < end) *p = 0;
    return patched ? (int)(p - out) : -1;
}

static int recompile_shader(GLuint shader, const char *src, int len)
{
    if (!real_glShaderSource) real_glShaderSource = (glShaderSource_t)trace_resolve("glShaderSource");
    if (!real_glCompileShader) real_glCompileShader = (glCompileShader_t)trace_resolve("glCompileShader");
    if (!real_glGetShaderiv) real_glGetShaderiv = (glGetShaderiv_t)trace_resolve("glGetShaderiv");
    if (!real_glShaderSource || !real_glCompileShader || !real_glGetShaderiv) return 0;
    real_glShaderSource(shader, 1, &src, &len);
    real_glCompileShader(shader);
    GLint ok = 0;
    real_glGetShaderiv(shader, 0x8B81, &ok);
    if (!ok) return 0;
    store_shader_src(shader, src, len);
    return 1;
}

static void WINAPI wrap_glAttachShader(GLuint program, GLuint shader)
{
    if (!real_glAttachShader) real_glAttachShader = (glAttachShader_t)trace_resolve("glAttachShader");
    if (program < 65536 && shader < 65536 && g_prog_shader_count[program] < 8) {
        g_prog_shaders[program][g_prog_shader_count[program]++] = shader;
    }
    glog("SHADER glAttachShader prog=%u shader=%u type=0x%x replaced=%d\n",
         program, shader,
         shader < 65536 ? g_shader_types[shader] : 0,
         shader < 65536 ? g_shader_replaced[shader] : 0);
    if (real_glAttachShader) real_glAttachShader(program, shader);
}

static void WINAPI wrap_glLinkProgram(GLuint program)
{
    if (!real_glLinkProgram) real_glLinkProgram = (glLinkProgram_t)trace_resolve("glLinkProgram");
    if (program < 65536) {
        /* Patch VertexData texcoord size mismatches (vertex vec2/3 vs fragment
           vec4, which NVIDIA tolerates but AMD rejects). */
        for (int vi = 0; vi < g_prog_shader_count[program] && vi < 8; vi++) {
            GLuint vs = g_prog_shaders[program][vi];
            if (vs >= 65536 || g_shader_types[vs] != 0x8B31) continue;
            int vt = shader_texcoord_type(g_shader_src[vs].src ? g_shader_src[vs].src : "",
                                          g_shader_src[vs].len);
            if (!vt) continue;
            for (int fi = 0; fi < g_prog_shader_count[program] && fi < 8; fi++) {
                GLuint fs = g_prog_shaders[program][fi];
                if (fs >= 65536 || g_shader_types[fs] != 0x8B30) continue;
                int ft = shader_texcoord_type(g_shader_src[fs].src ? g_shader_src[fs].src : "",
                                              g_shader_src[fs].len);
                if (!ft || ft == vt) continue;
                if (uses_texcoord_zw(g_shader_src[fs].src ? g_shader_src[fs].src : "",
                                     g_shader_src[fs].len)) {
                    glog("SHADER texcoord-mismatch prog=%u vert=%u(tc%d) frag=%u(tc%d) SKIP(.zw used)\n",
                         program, vs, vt, fs, ft);
                    continue;
                }
                int cap = g_shader_src[fs].len + 64;
                char *buf = (char *)HeapAlloc(GetProcessHeap(), 0, (size_t)cap);
                if (!buf) continue;
                int plen = patch_texcoord_type(g_shader_src[fs].src, g_shader_src[fs].len,
                                               buf, cap, vt);
                if (plen >= 0 && recompile_shader(fs, buf, plen)) {
                    glog("SHADER texcoord-patched prog=%u frag=%u tc%d->tc%d\n",
                         program, fs, ft, vt);
                }
                HeapFree(GetProcessHeap(), 0, buf);
            }
        }
        g_prog_ptrs[program].count = 0;
        for (int si = 0; si < g_prog_shader_count[program] && si < 8; si++) {
            GLuint s = g_prog_shaders[program][si];
            if (s < 65536) {
                if (g_shader_ptrs[s].needs_vbo_ssbo) g_prog_ptrs[program].needs_vbo_ssbo = 1;
                if (g_shader_ptrs[s].needs_light_ssbo) g_prog_ptrs[program].needs_light_ssbo = 1;
                if (g_shader_ptrs[s].needs_raw_ssbo) g_prog_ptrs[program].needs_raw_ssbo = 1;
                if (g_shader_ptrs[s].needs_emitter_count) {
                    g_prog_ptrs[program].needs_emitter_count = 1;
                    g_prog_ptrs[program].emitter_ubo_binding = g_shader_ptrs[s].emitter_ubo_binding;
                }
                if (g_shader_ptrs[s].emitter_struct_count > g_prog_ptrs[program].emitter_struct_count) {
                    for (int e = 0; e < 2; e++) {
                        g_prog_ptrs[program].emitter_struct_ubo[e] = g_shader_ptrs[s].emitter_struct_ubo[e];
                        g_prog_ptrs[program].emitter_struct_idx[e] = g_shader_ptrs[s].emitter_struct_idx[e];
                        g_prog_ptrs[program].emitter_struct_ssbo[e] = g_shader_ptrs[s].emitter_struct_ssbo[e];
                    }
                    g_prog_ptrs[program].emitter_struct_count = g_shader_ptrs[s].emitter_struct_count;
                }
                for (int pi = 0; pi < g_shader_ptrs[s].count && g_prog_ptrs[program].count < MAX_PTR_MEMBERS; pi++) {
                    ptr_member *pm = &g_shader_ptrs[s].m[pi];
                    int dup = 0;
                    for (int di = 0; di < g_prog_ptrs[program].count; di++) {
                        ptr_member *ex = &g_prog_ptrs[program].m[di];
                        if (ex->ubo_binding == pm->ubo_binding &&
                            ex->ssbo_binding == pm->ssbo_binding &&
                            strcmp(ex->name, pm->name) == 0) {
                            dup = 1;
                            break;
                        }
                    }
                    if (!dup) g_prog_ptrs[program].m[g_prog_ptrs[program].count++] = *pm;
                }
            }
        }
        glog("SHADER glLinkProgram prog=%u shaders=%d [", program, g_prog_shader_count[program]);
        for (int i = 0; i < g_prog_shader_count[program]; i++) {
            GLuint s = g_prog_shaders[program][i];
            glog("%s%u:t=0x%x:r=%d", i ? " " : "", s,
                 s < 65536 ? g_shader_types[s] : 0,
                 s < 65536 ? g_shader_replaced[s] : 0);
        }
        glog("] ptrs=%d", g_prog_ptrs[program].count);
        for (int pi = 0; pi < g_prog_ptrs[program].count; pi++) {
            glog(" %s@ubo%d->ssbo%d", g_prog_ptrs[program].m[pi].name,
                 g_prog_ptrs[program].m[pi].ubo_binding,
                 g_prog_ptrs[program].m[pi].ssbo_binding);
        }
        glog("\n");
    }
    if (real_glLinkProgram) real_glLinkProgram(program);
}

#define GL_DRAW_INDIRECT_BUFFER 0x8F3F

static GLuint g_attrib_stride[16];
static GLuint g_element_buffer;
static GLuint64 g_element_offset;

static FILE *g_gl_log;
static unsigned long long g_gl_log_lines;

static void glog(const char *fmt, ...)
{
    if (!g_log_on) return;
    va_list ap;
    if (!g_gl_log) g_gl_log = fopen("C:\\fgo\\_tools\\glshim\\glcalls.log", "a");
    if (!g_gl_log || g_gl_log_lines >= 3000000) return;
    va_start(ap, fmt);
    vfprintf(g_gl_log, fmt, ap);
    va_end(ap);
    fflush(g_gl_log);
    g_gl_log_lines++;
}

static uint64_t make_fake_addr(GLuint buffer, GLuint offset)
{
    /* marker 0x4647 in bits 63..48 | buffer in bits 47..32 | offset in 0..31 */
    return 0x4647000000000000ULL |
           ((uint64_t)(buffer & 0xFFFFu) << 32) |
           (uint64_t)(offset & 0xFFFFFFFFu);
}

static void decode_fake_addr(uint64_t addr, GLuint *buffer, GLuint *offset)
{
    *buffer = (GLuint)((addr >> 32) & 0xFFFFu);
    *offset = (GLuint)(addr & 0xFFFFFFFFu);
}

static int is_fake_addr(uint64_t addr)
{
    return (addr >> 48) == 0x4647ULL;
}

static void ensure_gl_bind_vertex_buffer(void)
{
    if (!real_glBindVertexBuffer && real_wglGetProcAddress)
        real_glBindVertexBuffer = (glBindVertexBuffer_t)real_wglGetProcAddress("glBindVertexBuffer");
}

static void ensure_gl_bind_buffer(void)
{
    if (!real_glBindBuffer && real_wglGetProcAddress)
        real_glBindBuffer = (glBindBuffer_t)real_wglGetProcAddress("glBindBuffer");
}

static void store_shader_src(GLuint id, const char *src, int len)
{
    if (id >= 65536) return;
    if (g_shader_src[id].src) HeapFree(GetProcessHeap(), 0, g_shader_src[id].src);
    char *c = (char *)HeapAlloc(GetProcessHeap(), 0, (size_t)len + 1);
    if (!c) { g_shader_src[id].src = NULL; g_shader_src[id].len = 0; return; }
    memcpy(c, src, (size_t)len);
    c[len] = 0;
    g_shader_src[id].src = c;
    g_shader_src[id].len = len;
}

static int contains_n(const char *s, int len, const char *needle)
{
    int nl = (int)strlen(needle);
    if (len < nl) return 0;
    for (int i = 0; i + nl <= len; i++) {
        if (memcmp(s + i, needle, nl) == 0) return 1;
    }
    return 0;
}

static int is_idchar(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_' || c == '.';
}

static int is_namechar(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
}

static int is_digit(char c)
{
    return c >= '0' && c <= '9';
}

/* Mark `const` tokens that are function parameters (i.e. inside a parameter
   list whose closing paren is followed by '{' and whose name is not a control
   keyword).  AMD rejects `const` in overloaded functions even when consistent. */
static void mark_param_const(const char *src, int len, unsigned char *skip)
{
    for (int i = 0; i + 5 <= len; i++) {
        if (memcmp(src + i, "const", 5) != 0) continue;
        if (i > 0 && is_namechar(src[i - 1])) continue;
        int j = i + 5;
        if (j >= len || (src[j] != ' ' && src[j] != '\t')) continue;
        /* find enclosing '(' */
        int depth = 0, lo = -1;
        for (int k = i; k >= 0; k--) {
            if (src[k] == ')') depth++;
            else if (src[k] == '(') {
                if (depth == 0) { lo = k; break; }
                depth--;
            }
        }
        if (lo < 0) continue;
        /* matching ')' */
        int d = 0, hi = -1;
        for (int k = lo; k < len; k++) {
            if (src[k] == '(') d++;
            else if (src[k] == ')') { d--; if (d == 0) { hi = k; break; } }
        }
        if (hi < 0) continue;
        int q = hi + 1;
        while (q < len && (src[q] == ' ' || src[q] == '\t' || src[q] == '\r' || src[q] == '\n')) q++;
        if (q >= len || src[q] != '{') continue;
        /* identifier immediately before '(' */
        int p = lo - 1;
        while (p >= 0 && (src[p] == ' ' || src[p] == '\t' || src[p] == '\r' || src[p] == '\n')) p--;
        int ne = p + 1;
        while (p >= 0 && is_namechar(src[p])) p--;
        int ns = p + 1;
        if (ns >= ne) continue;
        /* reject control keywords */
        static const char *kw[] = { "if", "while", "for", "switch", "catch", "return", NULL };
        int is_kw = 0;
        for (int k = 0; kw[k]; k++) {
            int kl = (int)strlen(kw[k]);
            if (ne - ns == kl && memcmp(src + ns, kw[k], (size_t)kl) == 0) { is_kw = 1; break; }
        }
        if (is_kw) continue;
        /* require a return type token before the name */
        int r = ns - 1;
        while (r >= 0 && (src[r] == ' ' || src[r] == '\t' || src[r] == '\r' || src[r] == '\n')) r--;
        if (r < 0 || !is_namechar(src[r])) continue;
        skip[i] = 1;
    }
}

/* Mark the second `#else` (or `#elif` after `#else`) at each nesting level:
   NVIDIA tolerates it, AMD rejects it.  The marked line is replaced with
   `#elif 0`, which is valid and preserves the (dead) branch selection. */
static void mark_double_else(const char *src, int len, unsigned char *repl)
{
    int depth = 0;
    int seen[32] = {0};
    int i = 0;
    while (i < len) {
        int j = i;
        while (j < len && (src[j] == ' ' || src[j] == '\t')) j++;
        if (j < len && src[j] == '#') {
            int k = j + 1;
            while (k < len && (src[k] == ' ' || src[k] == '\t')) k++;
            if (k + 2 <= len && memcmp(src + k, "if", 2) == 0 && (k + 2 >= len || !is_namechar(src[k + 2]))) {
                if (depth < 31) { depth++; seen[depth] = 0; }
            } else if (k + 5 <= len && memcmp(src + k, "ifdef", 5) == 0 && (k + 5 >= len || !is_namechar(src[k + 5]))) {
                if (depth < 31) { depth++; seen[depth] = 0; }
            } else if (k + 6 <= len && memcmp(src + k, "ifndef", 6) == 0 && (k + 6 >= len || !is_namechar(src[k + 6]))) {
                if (depth < 31) { depth++; seen[depth] = 0; }
            } else if (k + 5 <= len && memcmp(src + k, "endif", 5) == 0 && (k + 5 >= len || !is_namechar(src[k + 5]))) {
                if (depth > 0) { seen[depth] = 0; depth--; }
            } else if (k + 4 <= len && memcmp(src + k, "else", 4) == 0 && (k + 4 >= len || !is_namechar(src[k + 4]))) {
                if (seen[depth]) repl[j] = 1;
                else seen[depth] = 1;
            } else if (k + 4 <= len && memcmp(src + k, "elif", 4) == 0 && (k + 4 >= len || !is_namechar(src[k + 4]))) {
                if (seen[depth]) repl[j] = 1;
            }
        }
        while (i < len && src[i] != '\n') i++;
        if (i < len) i++;
    }
}

/* Returns 1 if src[i..] is `samplerXxx(...)` / `imageXxx(...)`; sets *end to
   the index after the matching closing paren of the constructor. */
static int handle_ctor_at(const char *s, int len, int i, int *out_end)
{
    static const char *ctors[] = {
        "sampler1D", "sampler2D", "sampler3D", "samplerCube",
        "sampler1DArray", "sampler2DArray", "sampler2DMS",
        "sampler2DMSArray", "samplerCubeArray", "samplerBuffer",
        "sampler2DRect", "sampler1DShadow", "sampler2DShadow",
        "samplerCubeShadow", "sampler1DArrayShadow", "sampler2DArrayShadow",
        "samplerCubeArrayShadow", "image1D", "image2D", "image3D",
        "imageCube", "image1DArray", "image2DArray", "imageBuffer",
        "image2DMS", "image2DMSArray", "imageCubeArray", NULL
    };
    for (int k = 0; ctors[k]; k++) {
        int cl = (int)strlen(ctors[k]);
        if (i + cl < len && memcmp(s + i, ctors[k], cl) == 0 && s[i + cl] == '(') {
            int j = i + cl + 1;
            int depth = 1;
            while (j < len && depth > 0) {
                if (s[j] == '(') depth++;
                else if (s[j] == ')') depth--;
                j++;
            }
            if (depth == 0) {
                *out_end = j;
                return 1;
            }
        }
    }
    return 0;
}

/* Detect a C-style cast `(TYPE)expr` where TYPE is a scalar/vector builtin.
   Returns the index of the expression start, or 0.  Sets *type_end to the
   index just after the type name. */
static int cast_expr_at(const char *s, int len, int i, int *type_end)
{
    static const char *types[] = {
        "int", "uint", "float", "bool", "double",
        "ivec2", "ivec3", "ivec4", "uvec2", "uvec3", "uvec4",
        "vec2", "vec3", "vec4", NULL
    };
    if (i >= len || s[i] != '(') return 0;
    int j = i + 1;
    while (j < len && (s[j] == ' ' || s[j] == '\t')) j++;
    int t0 = j;
    while (j < len && is_namechar(s[j])) j++;
    int t1 = j;
    if (t1 == t0) return 0;
    int tl = t1 - t0;
    int ok = 0;
    for (int k = 0; types[k]; k++) {
        if ((int)strlen(types[k]) == tl && memcmp(s + t0, types[k], (size_t)tl) == 0) {
            ok = 1;
            break;
        }
    }
    if (!ok) return 0;
    while (j < len && (s[j] == ' ' || s[j] == '\t')) j++;
    if (j >= len || s[j] != ')') return 0;
    int a = j + 1;
    while (a < len && (s[a] == ' ' || s[a] == '\t')) a++;
    if (a >= len) return 0;
    if (!is_namechar(s[a]) && s[a] != '(') return 0;
    *type_end = t1;
    return a;
}

static void log_shader_event(const char *tag, GLuint shader, const char *src, int len, GLenum stype)
{
    FILE *f = fopen("C:\\fgo\\_tools\\glshim\\shim.log", "a");
    if (!f) return;
    fprintf(f, "[%llu] %s shader=%u type=0x%x len=%d", (unsigned long long)GetTickCount64(), tag, shader, stype, len);
    if (len > 0) {
        int n = 0;
        while (n < len && n < 120 && src[n] != '\n') n++;
        if (n > 0) fprintf(f, " :: %.*s", n, src);
    }
    fprintf(f, "\n");
    fclose(f);
}

static void dump_shader_source(GLuint shader, const char *kind, const char *src, int len)
{
    char path[MAX_PATH];
    _snprintf(path, sizeof(path), "C:\\fgo\\_tools\\glshim\\shader_%u_%llu_%s.glsl",
              shader, (unsigned long long)GetTickCount64(), kind);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fwrite(src, 1, (size_t)len, f);
    fclose(f);
}

static int has_nv_pointer(const char *s, int len)
{
    int i;
    /* Generic GLSL pointer declaration: `Type* name` (star adjacent to the
       type, whitespace before the name).  NVIDIA accepts these only with
       GL_NV_shader_buffer_load; AMD rejects them outright. */
    for (i = 1; i + 1 < len; i++) {
        if (s[i] == '*' && is_namechar(s[i - 1])) {
            int j = i + 1;
            int ws = 0;
            while (j < len && (s[j] == ' ' || s[j] == '\t')) j++;
            if (j > i + 1) ws = 1;
            if (!ws || j >= len || !is_namechar(s[j])) continue;
            int ne = j;
            while (ne < len && is_namechar(s[ne])) ne++;
            if (ne < len && (s[ne] == ';' || s[ne] == ',' || s[ne] == '[' ||
                             s[ne] == '=' || s[ne] == ' ' || s[ne] == '\t')) return 1;
        }
    }
    return contains_n(s, len, "PackedVertex*") ||
           contains_n(s, len, "InputVertex*") ||
           contains_n(s, len, "PerSkinning") ||
           contains_n(s, len, "g_light_gpu_addr") ||
           contains_n(s, len, "packPtr") ||
           contains_n(s, len, "GL_NV_shader_buffer_load") ||
           contains_n(s, len, "GL_NV_shader_thread_group") ||
           contains_n(s, len, "GL_NV_shader_atomic_float");
}

/* Replace NVIDIA GLSL-pointer shaders with AMD-compilable no-ops that keep the
   same UBO/image interface, so the render-batch builder can proceed. */
static int make_nv_pointer_replacement(GLuint shader, GLenum stype, const char *src, int len, char *out, int cap)
{
    static const char compute_noop[] =
        "#version 430\n"
        "layout(local_size_x = 1) in;\n"
        "void main(){}\n";
    static const char tcs_noop[] =
        "#version 430\n"
        "layout(vertices = 1) out;\n"
        "void main(){ gl_out[gl_InvocationID].gl_Position = gl_in[gl_InvocationID].gl_Position; }\n";
    static const char tes_noop[] =
        "#version 430\n"
        "layout(triangles, equal_spacing, cw) in;\n"
        "void main(){ gl_Position = gl_in[0].gl_Position; }\n";
    static const char gs_noop[] =
        "#version 430\n"
        "layout(points) in;\n"
        "layout(points, max_vertices = 1) out;\n"
        "void main(){ gl_Position = gl_in[0].gl_Position; EmitVertex(); EndPrimitive(); }\n";
    static const char skinning[] =
        "#version 430\n"
        "layout(std140, binding = 0) uniform PerSkinning{\n"
        "    uvec2 g_output_vertices;\n"
        "    uvec2 g_input_vertices;\n"
        "    vec4 g_world_transforms[576];\n"
        "};\n"
        "void main(){\n"
        "    float x = float((gl_VertexID << 1) & 2) - 1.0;\n"
        "    float y = float((gl_VertexID & 2) << 1) - 1.0;\n"
        "    gl_Position = vec4(x, y, 0.0, 1.0);\n"
        "}\n";
    static const char frag_black[] =
        "#version 430\n"
        "layout(location = 0) out vec4 fragColor;\n"
        "void main(){ fragColor = vec4(1.0, 0.0, 1.0, 1.0); }\n";
    static const char vert_clip[] =
        "#version 430\n"
        "void main(){\n"
        "    float x = float((gl_VertexID << 1) & 2) - 1.0;\n"
        "    float y = float((gl_VertexID & 2) << 1) - 1.0;\n"
        "    gl_Position = vec4(x, y, 0.0, 1.0);\n"
        "}\n";
    const char *rep;
    const char *tag;

    if (stype == 0x91B9) {
        rep = compute_noop;
        tag = "compute";
    } else if (stype == 0x8E88) {
        rep = tcs_noop;
        tag = "tcs";
    } else if (stype == 0x8E87) {
        rep = tes_noop;
        tag = "tes";
    } else if (stype == 0x8DD9) {
        rep = gs_noop;
        tag = "gs";
    } else if (contains_n(src, len, "PackedVertex*") ||
        contains_n(src, len, "InputVertex*") ||
        contains_n(src, len, "PerSkinning")) {
        rep = skinning;
        tag = "skinning";
    } else if (stype == 0x8B30) {
        rep = frag_black;
        tag = "light";
    } else {
        rep = vert_clip;
        tag = "light";
    }

    int n = (int)strlen(rep);
    if (n + 1 > cap) return -1;
    memcpy(out, rep, (size_t)n + 1);
    log_shader_event("REPLACE", shader, rep, n, stype);
    dump_shader_source(shader, "orig", src, len);
    dump_shader_source(shader, "final", rep, n);
    return n;
}

/* Rewrite one GLSL source string. Returns new length (>=0) if changed, else -1. */
static int fix_shader(GLenum stype, const char *src, int len, char *out, int cap)
{
    char *p = out;
    char *end = out + cap - 1;
    unsigned char *skip = (unsigned char *)HeapAlloc(GetProcessHeap(), 0, len ? (size_t)len : 1);
    if (skip) {
        memset(skip, 0, len ? (size_t)len : 1);
        mark_param_const(src, len, skip);
    }
    unsigned char *repl = (unsigned char *)HeapAlloc(GetProcessHeap(), 0, len ? (size_t)len : 1);
    if (repl) {
        memset(repl, 0, len ? (size_t)len : 1);
        mark_double_else(src, len, repl);
    }
    int need_bindless = contains_n(src, len, "GL_NV_gpu_shader5") ||
                        contains_n(src, len, "GL_NV_bindless_texture");
    int already_bindless = contains_n(src, len, "GL_ARB_bindless_texture");
    int insert_at = -1;
    int changed = 0;
    int added = 0;

    if (need_bindless && !already_bindless) {
        /* insert right after the #version ... newline */
        for (int k = 0; k + 8 <= len; k++) {
            if (memcmp(src + k, "#version", 8) == 0) {
                int m = k;
                while (m < len && src[m] != '\n') m++;
                if (m < len) insert_at = m + 1;
                break;
            }
        }
    }

    int i = 0;
    while (i < len && p < end) {
        if (skip && skip[i]) {
            i += 5;
            changed = 1;
            continue;
        }
        if (repl && repl[i]) {
            /* drop the redundant second `#else` (or `#elif` after `#else`) line */
            while (i < len && src[i] != '\n') i++;
            changed = 1;
            continue;
        }
        if (i == insert_at && !added) {
            static const char ins[] = "#extension GL_ARB_bindless_texture : enable\n";
            for (int k = 0; ins[k] && p < end; k++) *p++ = ins[k];
            added = 1;
            changed = 1;
        }
        /* AMD rejects parenthesized local-size layout values that come from
           `#define NAME (N)`.  Strip the parens: `#define NAME (32)` -> `#define NAME 32`. */
        if (src[i] == '#') {
            int q = i + 1;
            if (q + 6 <= len && memcmp(src + q, "define", 6) == 0) {
                q += 6;
                while (q < len && (src[q] == ' ' || src[q] == '\t')) q++;
                int ns = q;
                while (q < len && is_namechar(src[q])) q++;
                if (q > ns) {
                    /* Force the active stage macro to 1 so the right #if branch
                       is compiled.  The game ships shared sources with
                       VERTEX_SHADER/TESSELLATION_SHADER defaulting to 0;
                       NVIDIA tolerates gl_FragCoord in vertex stages, AMD does
                       not. */
                    const char *stage_want = NULL;
                    if (stype == 0x8B31) stage_want = "VERTEX_SHADER";
                    else if (stype == 0x8E88 || stype == 0x8E87) stage_want = "TESSELLATION_SHADER";
                    else if (stype == 0x8DD9) stage_want = "GEOMETRY_SHADER";
                    else if (stype == 0x91B9) stage_want = "COMPUTE_SHADER";
                    int stage_nl = q - ns;
                    if (stage_want && (int)strlen(stage_want) == stage_nl &&
                        memcmp(src + ns, stage_want, (size_t)stage_nl) == 0) {
                        while (i < q && p < end) *p++ = src[i++];
                        static const char one[] = " 1";
                        for (int k = 0; one[k] && p < end; k++) *p++ = one[k];
                        while (q < len && src[q] != '\n') q++;
                        if (q < len && p < end) *p++ = src[q++];
                        i = q;
                        changed = 1;
                        continue;
                    }
                    while (q < len && (src[q] == ' ' || src[q] == '\t')) q++;
                    if (q < len && src[q] == '(') {
                        int k = q + 1;
                        int neg = 0;
                        if (k < len && (src[k] == '-' || src[k] == '+')) {
                            neg = (src[k] == '-');
                            k++;
                        }
                        int ds = k;
                        while (k < len && is_digit(src[k])) k++;
                        if (k > ds && k < len && src[k] == ')') {
                            while (i < q && p < end) *p++ = src[i++];
                            i = q + 1; /* skip '(' */
                            if (neg) {
                                if (p < end) *p++ = '-';
                                i++; /* skip sign */
                            }
                            while (i < k && p < end) *p++ = src[i++];
                            i = k + 1; /* skip ')' */
                            changed = 1;
                            continue;
                        }
                    }
                }
            }
        }
        if (src[i] == '(') {
            int te = 0;
            int es = cast_expr_at(src, len, i, &te);
            if (es > 0) {
                /* emit "TYPE(" -- drop the cast's opening paren so the
                   paren counts stay balanced: `(int)x` -> `int(x)` */
                i++; /* skip '(' */
                while (i < te && p < end) *p++ = src[i++];
                if (p < end) *p++ = '(';
                i = es;
                if (src[i] == '(') {
                    int d = 1, j = i + 1;
                    while (j < len && d) {
                        if (src[j] == '(') d++;
                        else if (src[j] == ')') d--;
                        j++;
                    }
                    while (i < j && p < end) *p++ = src[i++];
                } else {
                    while (i < len && p < end) {
                        if (is_namechar(src[i])) {
                            *p++ = src[i++];
                        } else if (src[i] == '.') {
                            *p++ = src[i++];
                        } else if (src[i] == '[') {
                            int d = 1, j = i + 1;
                            while (j < len && d) {
                                if (src[j] == '[') d++;
                                else if (src[j] == ']') d--;
                                j++;
                            }
                            while (i < j && p < end) *p++ = src[i++];
                        } else {
                            break;
                        }
                    }
                }
                if (p < end) *p++ = ')';
                changed = 1;
                continue;
            }
        }
        int cend;
        if (handle_ctor_at(src, len, i, &cend)) {
            int j = i;
            while (j < len && src[j] != '(') j++;
            int astart = j + 1;
            /* Already-ARB form `samplerXxx(unpackUint2x32(...))` must not be
               wrapped again (double unpack fails to compile). */
            int already = (cend - astart >= 15 &&
                           memcmp(src + astart, "unpackUint2x32(", 15) == 0);
            if (already) {
                while (i < cend && p < end) *p++ = src[i++];
            } else {
                /* copy "samplerXxx(" */
                while (i < astart && p < end) *p++ = src[i++];
                static const char pre[] = "unpackUint2x32(";
                for (int k = 0; pre[k] && p < end; k++) *p++ = pre[k];
                /* copy the full argument (everything before the ctor's ')') */
                while (i < cend - 1 && p < end) *p++ = src[i++];
                static const char post[] = "))";
                for (int k = 0; post[k] && p < end; k++) *p++ = post[k];
                i = cend;
                changed = 1;
            }
        } else {
            *p++ = src[i++];
        }
    }
    if (need_bindless && !already_bindless && !added) {
        static const char ins[] = "\n#extension GL_ARB_bindless_texture : enable\n";
        for (int k = 0; ins[k] && p < end; k++) *p++ = ins[k];
        changed = 1;
    }
    if (p < end) *p = 0;
    if (skip) HeapFree(GetProcessHeap(), 0, skip);
    if (repl) HeapFree(GetProcessHeap(), 0, repl);
    return changed ? (int)(p - out) : -1;
}

/* ---------- NVIDIA GLSL pointer -> SSBO rewrite ---------- */

static int g_macro_n;
static int g_macro_val[256];
static char g_macro_name[256][64];

static int resolve_binding_name(const char *s, int len)
{
    char nm[64];
    int n0 = 0;
    while (n0 < len && is_namechar(s[n0])) n0++;
    if (n0 == 0 || n0 >= 64) return -1;
    memcpy(nm, s, (size_t)n0);
    nm[n0] = 0;
    for (int i = 0; i < g_macro_n; i++) {
        if (strcmp(nm, g_macro_name[i]) == 0) return g_macro_val[i];
    }
    return -1;
}

static void collect_macros(const char *s, int len)
{
    g_macro_n = 0;
    for (int i = 0; i + 7 < len && g_macro_n < 256; i++) {
        if (memcmp(s + i, "#define", 7) != 0) continue;
        int j = i + 7;
        while (j < len && (s[j] == ' ' || s[j] == '\t')) j++;
        int n0 = j;
        while (j < len && is_namechar(s[j])) j++;
        int n1 = j;
        if (n1 == n0) continue;
        while (j < len && (s[j] == ' ' || s[j] == '\t')) j++;
        int v = 0, ds = j;
        while (j < len && is_digit(s[j])) { v = v * 10 + (s[j] - '0'); j++; }
        if (j == ds) continue;
        int nl = n1 - n0;
        if (nl >= 64) nl = 63;
        memcpy(g_macro_name[g_macro_n], s + n0, (size_t)nl);
        g_macro_name[g_macro_n][nl] = 0;
        g_macro_val[g_macro_n] = v;
        g_macro_n++;
        i = j;
    }
}

static int find_ubo_block(const char *s, int len, int pos,
                          int *bs, int *bo, int *bstart, int *bend, int *bend2, int *binding)
{
    for (int i = pos; i + 7 < len; i++) {
        if (memcmp(s + i, "layout", 6) != 0) continue;
        if (i > 0 && is_namechar(s[i - 1])) continue;
        int j = i + 6;
        while (j < len && s[j] != '(') { if (s[j] == ';') break; j++; }
        if (j >= len || s[j] != '(') continue;
        int d = 1, k = j + 1;
        while (k < len && d) {
            if (s[k] == '(') d++;
            else if (s[k] == ')') d--;
            k++;
        }
        if (d) continue;
        int layout_end = k;
        int u = k;
        while (u < len && (s[u] == ' ' || s[u] == '\t' || s[u] == '\r' || s[u] == '\n')) u++;
        if (u + 7 > len || memcmp(s + u, "uniform", 7) != 0) continue;
        u += 7;
        int nm = u;
        while (nm < len && (s[nm] == ' ' || s[nm] == '\t' || s[nm] == '\r' || s[nm] == '\n')) nm++;
        int nend = nm;
        while (nend < len && is_namechar(s[nend])) nend++;
        int br = nend;
        while (br < len && (s[br] == ' ' || s[br] == '\t' || s[br] == '\r' || s[br] == '\n')) br++;
        if (br >= len || s[br] != '{') continue;
        int bval = -1;
        for (int q = i; q + 7 <= layout_end; q++) {
            if (memcmp(s + q, "binding", 7) == 0 &&
                !(q > 0 && is_namechar(s[q - 1])) &&
                !(q + 7 < layout_end && is_namechar(s[q + 7]))) {
                int r = q + 7;
                while (r < layout_end && (s[r] == ' ' || s[r] == '\t' || s[r] == '=')) r++;
                int ns = r, v = 0;
                while (r < layout_end && is_digit(s[r])) { v = v * 10 + (s[r] - '0'); r++; }
                if (r > ns) {
                    bval = v;
                } else if (r < layout_end && is_namechar(s[r])) {
                    bval = resolve_binding_name(s + r, layout_end - r);
                }
                break;
            }
        }
        *bs = i;
        *bo = br;
        *bstart = br + 1;
        d = 1;
        k = br + 1;
        while (k < len && d) {
            if (s[k] == '{') d++;
            else if (s[k] == '}') d--;
            k++;
        }
        if (d) continue;
        *bend = k - 1;
        int e = k;
        while (e < len && (s[e] == ' ' || s[e] == '\t' || s[e] == '\r' || s[e] == '\n')) e++;
        if (e < len && s[e] == ';') e++;
        *bend2 = e;
        *binding = bval;
        return 1;
    }
    return 0;
}

/* Parse `Type* name;` starting at/after pos inside a UBO body. */
static int parse_pointer_member(const char *b, int blen, int pos,
                                int *mstart, int *mend, char *type, int tcap,
                                char *name, int ncap)
{
    int i = pos;
    while (i < blen && (b[i] == ' ' || b[i] == '\t' || b[i] == '\r' || b[i] == '\n')) i++;
    int ts = i;
    if (i + 5 <= blen && memcmp(b + i, "const", 5) == 0 && !is_namechar(b[i + 5])) {
        i += 5;
        while (i < blen && (b[i] == ' ' || b[i] == '\t')) i++;
    }
    int t0 = i;
    while (i < blen && is_namechar(b[i])) i++;
    int t1 = i;
    if (t1 == t0) return 0;
    while (i < blen && (b[i] == ' ' || b[i] == '\t')) i++;
    if (i >= blen || b[i] != '*') return 0;
    i++;
    while (i < blen && (b[i] == ' ' || b[i] == '\t')) i++;
    int n0 = i;
    while (i < blen && is_namechar(b[i])) i++;
    int n1 = i;
    if (n1 == n0) return 0;
    while (i < blen && (b[i] == ' ' || b[i] == '\t')) i++;
    if (i >= blen || b[i] != ';') return 0;
    int tl = t1 - t0;
    if (tl >= tcap) tl = tcap - 1;
    memcpy(type, b + t0, (size_t)tl);
    type[tl] = 0;
    int nl = n1 - n0;
    if (nl >= ncap) nl = ncap - 1;
    memcpy(name, b + n0, (size_t)nl);
    name[nl] = 0;
    *mstart = ts;
    *mend = i + 1;
    return 1;
}

/* Record which struct types contain pointer members (nested pointers). */
static int collect_unsafe_structs(const char *s, int len, char unsafe[][64], int maxn, int *n_out)
{
    int n = 0;
    int pos = 0;
    while (pos < len && n < maxn) {
        int i = pos;
        while (i < len && !(memcmp(s + i, "struct", 6) == 0 &&
                            (i == 0 || !is_namechar(s[i - 1])) &&
                            (i + 6 >= len || !is_namechar(s[i + 6])))) i++;
        if (i >= len) break;
        int j = i + 6;
        while (j < len && (s[j] == ' ' || s[j] == '\t' || s[j] == '\r' || s[j] == '\n')) j++;
        int n0 = j;
        while (j < len && is_namechar(s[j])) j++;
        int n1 = j;
        while (j < len && (s[j] == ' ' || s[j] == '\t' || s[j] == '\r' || s[j] == '\n')) j++;
        if (j >= len || s[j] != '{') { pos = i + 6; continue; }
        int d = 1, k = j + 1;
        while (k < len && d) {
            if (s[k] == '{') d++;
            else if (s[k] == '}') d--;
            k++;
        }
        if (d) { pos = i + 6; continue; }
        int body_has_ptr = 0;
        int mp = j + 1;
        while (mp < k - 1) {
            int ms, me;
            char t[64], nm[64];
            if (parse_pointer_member(s, k - 1, mp, &ms, &me, t, sizeof t, nm, sizeof nm)) {
                body_has_ptr = 1;
                break;
            }
            mp++;
        }
        if (body_has_ptr && n1 > n0) {
            int nl = n1 - n0;
            if (nl >= 64) nl = 63;
            memcpy(unsafe[n], s + n0, (size_t)nl);
            unsafe[n][nl] = 0;
            n++;
        }
        pos = k;
    }
    *n_out = n;
    return n;
}

static int is_unsafe_type(const char *type, char unsafe[][64], int n)
{
    for (int i = 0; i < n; i++) {
        if (strcmp(type, unsafe[i]) == 0) return 1;
    }
    return 0;
}

/* Rewrite local NVIDIA pointer usage (BLEND_SHAPE `packPtr` + read_vertex,
   and the emitter `cnt_addr` atomic) into SSBO indexed access.  Returns new
   length, or -1 if not applicable. */
static int rewrite_vbo_pointers(int shader, const char *src, int len, char *out, int cap)
{
    static const char *const pats[] = {
        "const float* vbo_addr = (const float*)packPtr(t.xy);",
        "Vertex read_vertex( float* vtx_addr,",
        "Vertex read_vertex(const float* vtx_addr,",
        "read_vertex_pnct4( float* vtx_addr",
        "read_vertex_pnct4(const float* vtx_addr",
        "const float* vertices = vtx_addr;",
        "const float* vtx_addr = vbo_addr",
        "vertices[",
        "int* cnt_addr = (int*)((uint64_t*)(g_emitters) - 1);",
        "atomicAdd(cnt_addr, -1)",
        "atomicAdd(cnt_addr, 1)",
    };
    static const char *const reps[] = {
        "uint vbo_addr = t.x / 4u;",
        "Vertex read_vertex( uint vtx_addr,",
        "Vertex read_vertex( uint vtx_addr,",
        "read_vertex_pnct4( uint vtx_addr",
        "read_vertex_pnct4(uint vtx_addr",
        "uint vertices = vtx_addr;",
        "uint vtx_addr = vbo_addr",
        "vbo_data[vertices + ",
        "// cnt_addr removed",
        "atomicAdd(g_emitter_count[0], -1)",
        "atomicAdd(g_emitter_count[0], 1)",
    };
    int np = (int)(sizeof(pats) / sizeof(pats[0]));
    int found_vbo = contains_n(src, len, "packPtr");
    int found_cnt = contains_n(src, len, "cnt_addr");
    if (!found_vbo && !found_cnt) return -1;

    int insert_at = -1;
    for (int k = 0; k + 8 <= len; k++) {
        if (memcmp(src + k, "#version", 8) == 0) {
            int m = k;
            while (m < len && src[m] != '\n') m++;
            if (m < len) insert_at = m + 1;
            break;
        }
    }
    int decls_emitted = 0;
    char *p = out;
    int pos = 0;
    while (pos < len && p < out + cap - 1) {
        if (pos == insert_at && !decls_emitted) {
            if (found_vbo) {
                static const char decl[] = "layout(std430, binding = 29) buffer VboData { float vbo_data[]; };\n";
                for (int k = 0; decl[k] && p < out + cap - 1; k++) *p++ = decl[k];
                g_shader_ptrs[shader].needs_vbo_ssbo = 1;
            }
            if (found_cnt) {
                static const char edecl[] = "layout(std430, binding = 32) buffer EmitterCountBuf { int g_emitter_count[]; };\n";
                for (int k = 0; edecl[k] && p < out + cap - 1; k++) *p++ = edecl[k];
                for (int i = 0; i < g_shader_ptrs[shader].count; i++) {
                    if (strcmp(g_shader_ptrs[shader].m[i].name, "g_emitters") == 0) {
                        g_shader_ptrs[shader].needs_emitter_count = 1;
                        g_shader_ptrs[shader].emitter_ubo_binding = g_shader_ptrs[shader].m[i].ubo_binding;
                        break;
                    }
                }
            }
            decls_emitted = 1;
        }
        int bestr = -1;
        for (int r = 0; r < np; r++) {
            int pl = (int)strlen(pats[r]);
            if (pos + pl <= len && memcmp(src + pos, pats[r], (size_t)pl) == 0) {
                bestr = r;
                break;
            }
        }
        if (bestr < 0) {
            *p++ = src[pos++];
            continue;
        }
        int pl = (int)strlen(pats[bestr]);
        int rl = (int)strlen(reps[bestr]);
        for (int k = 0; k < rl && p < out + cap - 1; k++) *p++ = reps[bestr][k];
        pos += pl;
    }
    if (p < out + cap) *p = 0;
    return (int)(p - out);
}

/* Rewrite the tile-light shaders' NV pointer uniforms (g_light_gpu_addr /
   g_lights_addr) into SSBO 36 indexed access.  The Light struct is defined
   before main in both shaders, so the SSBO declaration is inserted right
   after the struct block.  Previously these shaders were PTRREJECTed and
   replaced with a dummy that drew nothing. */
static int rewrite_light_pointers(int shader, const char *src, int len, char *out, int cap)
{
    static const char *const pats[] = {
        "const Light* ls = (const Light*)g_light_gpu_addr;",
        "Light* lights = (Light*)g_lights_addr;",
    };
    static const char *const use_pats[] = { "ls[", "lights[" };
    static const char *const use_reps[] = { "g_light_data[", "g_light_data[" };
    int any = 0;
    for (int r = 0; r < 2; r++) {
        if (contains_n(src, len, pats[r])) any = 1;
    }
    if (!any) return -1;

    /* Locate `struct Light { ... };` */
    int send = -1;
    for (int i = 0; i + 11 <= len; i++) {
        if (memcmp(src + i, "struct", 6) != 0) continue;
        if (i > 0 && is_namechar(src[i - 1])) continue;
        if (is_namechar(src[i + 6])) continue;
        int j = i + 6;
        while (j < len && (src[j] == ' ' || src[j] == '\t' || src[j] == '\r' || src[j] == '\n')) j++;
        if (j + 5 > len || memcmp(src + j, "Light", 5) != 0) continue;
        if (is_namechar(src[j + 5])) continue;
        int k = j + 5;
        while (k < len && (src[k] == ' ' || src[k] == '\t' || src[k] == '\r' || src[k] == '\n')) k++;
        if (k >= len || src[k] != '{') continue;
        int d = 1, m = k + 1;
        while (m < len && d) {
            if (src[m] == '{') d++;
            else if (src[m] == '}') d--;
            m++;
        }
        if (d) continue;
        int e = m;
        while (e < len && (src[e] == ' ' || src[e] == '\t' || src[e] == '\r' || src[e] == '\n')) e++;
        if (e < len && src[e] == ';') e++;
        send = e;
        break;
    }
    if (send < 0) return -1;

    static const char decl[] =
        "layout(std430, binding = 36) buffer LightData { Light g_light_data[]; };\n";
    char *p = out;
    int pos = 0;
    int decls = 0;
    while (pos < len && p < out + cap - 1) {
        if (pos == send && !decls) {
            for (int k = 0; decl[k] && p < out + cap - 1; k++) *p++ = decl[k];
            g_shader_ptrs[shader].needs_light_ssbo = 1;
            decls = 1;
        }
        int bestr = -1;
        for (int r = 0; r < 2; r++) {
            int pl = (int)strlen(pats[r]);
            if (pos + pl <= len && memcmp(src + pos, pats[r], (size_t)pl) == 0) {
                bestr = r;
                break;
            }
        }
        if (bestr >= 0) {
            pos += (int)strlen(pats[bestr]);
            continue;
        }
        int bu = -1;
        for (int r = 0; r < 2; r++) {
            int pl = (int)strlen(use_pats[r]);
            if (pos + pl <= len && memcmp(src + pos, use_pats[r], (size_t)pl) == 0) {
                bu = r;
                break;
            }
        }
        if (bu >= 0) {
            int rl = (int)strlen(use_reps[bu]);
            for (int k = 0; k < rl && p < out + cap - 1; k++) *p++ = use_reps[bu][k];
            pos += (int)strlen(use_pats[bu]);
            continue;
        }
        *p++ = src[pos++];
    }
    if (p < out + cap) *p = 0;
    return (int)(p - out);
}

typedef struct {
    char name[64];
    int start;
    int end;
} unsafe_def;

/* Find `struct NAME { ... };` definitions whose body contains pointer members
   (only valid on NVIDIA with GL_NV_shader_buffer_load). */
static int collect_unsafe_defs(const char *s, int len, unsafe_def *defs, int maxn)
{
    int n = 0, pos = 0;
    while (pos < len && n < maxn) {
        int i = pos;
        while (i < len && !(memcmp(s + i, "struct", 6) == 0 &&
                            (i == 0 || !is_namechar(s[i - 1])) &&
                            (i + 6 >= len || !is_namechar(s[i + 6])))) i++;
        if (i >= len) break;
        int j = i + 6;
        while (j < len && (s[j] == ' ' || s[j] == '\t' || s[j] == '\r' || s[j] == '\n')) j++;
        int n0 = j;
        while (j < len && is_namechar(s[j])) j++;
        int n1 = j;
        while (j < len && (s[j] == ' ' || s[j] == '\t' || s[j] == '\r' || s[j] == '\n')) j++;
        if (j >= len || s[j] != '{') {
            pos = i + 6;
            continue;
        }
        int d = 1, k = j + 1;
        while (k < len && d) {
            if (s[k] == '{') d++;
            else if (s[k] == '}') d--;
            k++;
        }
        if (d) {
            pos = i + 6;
            continue;
        }
        int body_has_ptr = 0;
        int mp = j + 1;
        while (mp < k - 1) {
            int ms, me;
            char t[64], nm[64];
            if (parse_pointer_member(s, k - 1, mp, &ms, &me, t, sizeof t, nm, sizeof nm)) {
                body_has_ptr = 1;
                break;
            }
            mp++;
        }
        if (body_has_ptr && n1 > n0) {
            int e = k;
            while (e < len && (s[e] == ' ' || s[e] == '\t' || s[e] == '\r' || s[e] == '\n')) e++;
            if (e < len && s[e] == ';') e++;
            defs[n].start = i;
            defs[n].end = e;
            int nl = n1 - n0;
            if (nl >= 64) nl = 63;
            memcpy(defs[n].name, s + n0, (size_t)nl);
            defs[n].name[nl] = 0;
            n++;
            pos = e;
        } else {
            pos = k;
        }
    }
    return n;
}

static int name_used_outside(const char *s, int len, const char *nm, int nl,
                             int start, int end)
{
    for (int i = 0; i + nl <= len; i++) {
        if (i >= start && i < end) {
            i = end - 1;
            continue;
        }
        if (memcmp(s + i, nm, (size_t)nl) == 0 &&
            (i == 0 || !is_namechar(s[i - 1])) &&
            !is_namechar(s[i + nl])) {
            return 1;
        }
    }
    return 0;
}

/* Remove struct definitions that contain pointer members and are no longer
   referenced anywhere (AMD rejects pointer types even inside structs). */
static int strip_unused_unsafe_structs(const char *src, int len, char *out, int cap)
{
    unsafe_def defs[16];
    int nd = collect_unsafe_defs(src, len, defs, 16);
    if (nd == 0) return -1;
    int changed = 0;
    char *p = out;
    int pos = 0;
    while (pos < len && p < out + cap - 1) {
        int rem = -1;
        for (int d = 0; d < nd; d++) {
            if (defs[d].start == pos) {
                if (!name_used_outside(src, len, defs[d].name,
                                       (int)strlen(defs[d].name),
                                       defs[d].start, defs[d].end)) {
                    rem = d;
                }
                break;
            }
        }
        if (rem >= 0) {
            pos = defs[rem].end;
            changed = 1;
            continue;
        }
        *p++ = src[pos++];
    }
    if (p < out + cap) *p = 0;
    return changed ? (int)(p - out) : -1;
}

/* Local-only pointer rewrite: BLEND_SHAPE packPtr/read_vertex usage plus
   unused unsafe-struct removal.  Used when the UBO member pass found nothing. */
static int rewrite_local_pointers(int shader, const char *src, int len, char *out, int cap)
{
    int llen = rewrite_light_pointers(shader, src, len, out, cap);
    if (llen >= 0) return llen;
    char *tmp = (char *)HeapAlloc(GetProcessHeap(), 0, (size_t)cap);
    if (!tmp) return -1;
    int vlen = rewrite_vbo_pointers(shader, src, len, tmp, cap);
    const char *base = src;
    int blen = len;
    if (vlen >= 0) {
        base = tmp;
        blen = vlen;
    }
    int slen = strip_unused_unsafe_structs(base, blen, out, cap);
    int outlen = -1;
    if (slen >= 0) {
        outlen = slen;
    } else if (vlen >= 0) {
        memcpy(out, tmp, (size_t)vlen + 1);
        outlen = vlen;
    }
    HeapFree(GetProcessHeap(), 0, tmp);
    return outlen;
}

/* Rewrite `g_unused0->m_count` / `g_unused0->m_indices[...]` (and the
   g_unused1 variant) into dedicated SSBOs.  The shim binds those SSBOs by
   following the struct pointer stored in the emitter UBO. */
static int rewrite_emitter_struct_uses(int shader, const char *src, int len,
                                       char *out, int cap,
                                       int ub0, int idx0, int ub1, int idx1)
{
    static const char *const pats[] = {
        "g_unused0->m_count", "g_unused0->m_indices[",
        "g_unused1->m_count", "g_unused1->m_indices[",
    };
    static const char *const reps[] = {
        "g_emitter_count[0]", "g_emitter_indices[",
        "g_emitter_count1[0]", "g_emitter_indices1[",
    };
    int np = (int)(sizeof(pats) / sizeof(pats[0]));
    int any = 0;
    for (int r = 0; r < np; r++) {
        if (contains_n(src, len, pats[r])) {
            any = 1;
            break;
        }
    }
    if (!any) return -1;

    int insert_at = -1;
    for (int k = 0; k + 8 <= len; k++) {
        if (memcmp(src + k, "#version", 8) == 0) {
            int m = k;
            while (m < len && src[m] != '\n') m++;
            if (m < len) insert_at = m + 1;
            break;
        }
    }
    char *p = out;
    int pos = 0;
    int decls = 0;
    while (pos < len && p < out + cap - 1) {
        if (pos == insert_at && !decls) {
            if (contains_n(src, len, "g_unused0->")) {
                static const char d0[] =
                    "layout(std430, binding = 32) buffer EmitterCountBuf { int g_emitter_count[]; };\n"
                    "layout(std430, binding = 33) buffer EmitterIndicesBuf { uint g_emitter_indices[]; };\n";
                for (int k = 0; d0[k] && p < out + cap - 1; k++) *p++ = d0[k];
                g_shader_ptrs[shader].emitter_struct_ubo[0] = ub0;
                g_shader_ptrs[shader].emitter_struct_idx[0] = idx0;
                g_shader_ptrs[shader].emitter_struct_ssbo[0] = 32;
                g_shader_ptrs[shader].emitter_struct_count = 1;
            }
            if (contains_n(src, len, "g_unused1->")) {
                static const char d1[] =
                    "layout(std430, binding = 34) buffer EmitterCount1Buf { int g_emitter_count1[]; };\n"
                    "layout(std430, binding = 35) buffer EmitterIndices1Buf { uint g_emitter_indices1[]; };\n";
                for (int k = 0; d1[k] && p < out + cap - 1; k++) *p++ = d1[k];
                g_shader_ptrs[shader].emitter_struct_ubo[1] = ub1;
                g_shader_ptrs[shader].emitter_struct_idx[1] = idx1;
                g_shader_ptrs[shader].emitter_struct_ssbo[1] = 34;
                if (g_shader_ptrs[shader].emitter_struct_count < 2)
                    g_shader_ptrs[shader].emitter_struct_count = 2;
            }
            decls = 1;
        }
        int bestr = -1;
        for (int r = 0; r < np; r++) {
            int pl = (int)strlen(pats[r]);
            if (pos + pl <= len && memcmp(src + pos, pats[r], (size_t)pl) == 0) {
                bestr = r;
                break;
            }
        }
        if (bestr < 0) {
            *p++ = src[pos++];
            continue;
        }
        int pl = (int)strlen(pats[bestr]);
        int rl = (int)strlen(reps[bestr]);
        for (int k = 0; k < rl && p < out + cap - 1; k++) *p++ = reps[bestr][k];
        pos += pl;
    }
    if (p < out + cap) *p = 0;
    return (int)(p - out);
}

/* Post-pass for the particle-sim shaders (prog 2611/2622/2626/2630).  The
   UBO->SSBO rewrite leaves NV pointer arithmetic behind:
     - `uint* get_particle_vec_w_ptr(...)` + atomicAdd through it
     - `float* p = (float*)(g_candidate_cells + i)` + atomicAdd(p+N, float)
     - the emitter free-list init via `uint* p32 = (uint*)(g_unused_ptr)`
   AMD rejects pointer types, so convert each to direct SSBO indexing. */
static int rewrite_particle_nv_arith(int shader, const char *src, int len,
                                     char *out, int cap)
{
    static const char *const decls =
        "#extension GL_ARB_gpu_shader_int64 : enable\n"
        "#extension GL_EXT_shader_atomic_float : enable\n"
        "layout(std430, binding = 30) buffer PtrBufU32_0 { uint g_particle_u32[]; };\n"
        "layout(std430, binding = 32) buffer PtrBufF_2 { float g_candidate_cell_float[]; };\n"
        "layout(std430, binding = 37) buffer EmitterRaw { uint g_emitter_raw[]; };\n";

    int has_wptr = contains_n(src, len, "uint* get_particle_vec_w_ptr(");
    int has_cand = contains_n(src, len, "(float*)(g_candidate_cells +");
    int has_reset = contains_n(src, len, "(uint*)(g_unused_ptr)");
    if (!has_wptr && !has_cand && !has_reset) return -1;

    int insert_at = -1;
    for (int k = 0; k + 8 <= len; k++) {
        if (memcmp(src + k, "#version", 8) == 0) {
            int m = k;
            while (m < len && src[m] != '\n') m++;
            if (m < len) insert_at = m + 1;
            break;
        }
    }

    char *p = out;
    int pos = 0;
    int decls_emitted = 0;
    while (pos < len && p < out + cap - 1) {
        if (pos == insert_at && !decls_emitted) {
            for (int k = 0; decls[k] && p < out + cap - 1; k++) *p++ = decls[k];
            decls_emitted = 1;
        }
        if (has_wptr && pos + 28 <= len &&
            memcmp(src + pos, "uint* get_particle_vec_w_ptr(", 28) == 0) {
            /* skip the whole helper function body */
            int j = pos;
            while (j < len && src[j] != '{') j++;
            if (j < len) {
                int d = 0, k = j;
                while (k < len) {
                    if (src[k] == '{') d++;
                    else if (src[k] == '}') {
                        d--;
                        if (d == 0) { k++; break; }
                    }
                    k++;
                }
                if (d == 0) {
                    g_shader_ptrs[shader].needs_raw_ssbo = 1;
                    pos = k;
                    continue;
                }
            }
        }
        static const struct { const char *pat; const char *rep; } reps[] = {
            { "atomicAdd(get_particle_vec_w_ptr(gl_VertexID), 1);",
              "atomicAdd(g_particle_u32[gl_VertexID * g_particle_size_in_u32 + g_offset_to_velocity_w_in_u32], 1u);" },
            { "atomicAdd(get_particle_vec_w_ptr(parent_idx), -1);",
              "atomicAdd(g_particle_u32[parent_idx * g_particle_size_in_u32 + g_offset_to_velocity_w_in_u32], 0xFFFFFFFFu);" },
            { "float* p = (float*)(g_candidate_cells + cell_seq_idx);",
              "// candidate cell float view via SSBO 32" },
            { "atomicAdd(p + 0, position.x);",
              "atomicAdd(g_candidate_cell_float[cell_seq_idx * 4 + 0], position.x);" },
            { "atomicAdd(p + 1, position.y);",
              "atomicAdd(g_candidate_cell_float[cell_seq_idx * 4 + 1], position.y);" },
            { "atomicAdd(p + 2, position.z);",
              "atomicAdd(g_candidate_cell_float[cell_seq_idx * 4 + 2], position.z);" },
            { "atomicAdd(p + 3, 1.f);",
              "atomicAdd(g_candidate_cell_float[cell_seq_idx * 4 + 3], 1.f);" },
            { "uint* p32 = (uint*)(g_unused_ptr);",
              "uint64_t g_unused_addr64 = packUint2x32(uvec2(floatBitsToUint(g_constants[0].x), floatBitsToUint(g_constants[0].y)));\n"
              "#define g_count_addr64 (g_unused_addr64 + 16ul)\n"
              "#define g_indices_addr64 (g_unused_addr64 + 20ul)" },
            { "uint* count_addr = p32 + unused_struct_size_in_u32;",
              "// count_addr -> g_count_addr64" },
            { "uint* indices_addr = p32 + unused_struct_size_in_u32 + count_size_in_u32;",
              "// indices_addr -> g_indices_addr64" },
            { "uint64_t* p64 = (uint64_t*)g_unused_ptr;",
              "// p64 removed (raw view via SSBO 37)" },
            { "p64[0] = (uint64_t)(count_addr);",
              "uvec2 caddr = unpackUint2x32(g_count_addr64); g_emitter_raw[0] = caddr.x; g_emitter_raw[1] = caddr.y;" },
            { "p64[1] = (uint64_t)(indices_addr);",
              "uvec2 iaddr = unpackUint2x32(g_indices_addr64); g_emitter_raw[2] = iaddr.x; g_emitter_raw[3] = iaddr.y;" },
            { "*count_addr = g_particle_capacity;",
              "g_emitter_raw[4] = uint(g_particle_capacity);" },
            { "indices_addr[gl_VertexID] = gl_VertexID;",
              "g_emitter_raw[5 + gl_VertexID] = gl_VertexID;" },
        };
        int matched = 0;
        for (size_t r = 0; r < sizeof(reps) / sizeof(reps[0]); r++) {
            int pl = (int)strlen(reps[r].pat);
            if (pos + pl <= len && memcmp(src + pos, reps[r].pat, (size_t)pl) == 0) {
                if (has_reset && reps[r].pat[0] == 'u' && reps[r].pat[1] == 'i' &&
                    reps[r].pat[2] == 'n' && reps[r].pat[3] == 't' &&
                    reps[r].pat[4] == '*' && reps[r].pat[5] == ' ') {
                    g_shader_ptrs[shader].needs_raw_ssbo = 1;
                }
                int rl = (int)strlen(reps[r].rep);
                for (int k = 0; k < rl && p < out + cap - 1; k++) *p++ = reps[r].rep[k];
                pos += pl;
                matched = 1;
                break;
            }
        }
        if (matched) continue;
        *p++ = src[pos++];
    }
    if (p < out + cap) *p = 0;
    return (int)(p - out);
}

/* Rewrite pointer members in UBO blocks into SSBO array bindings.
   Returns new length, or -1 if the shader cannot be rewritten safely. */
static int rewrite_pointer_shaders(GLuint shader, const char *src, int len, char *out, int cap)
{
    collect_macros(src, len);
    char unsafe[16][64];
    int n_unsafe = 0;
    collect_unsafe_structs(src, len, unsafe, 16, &n_unsafe);

    g_shader_ptrs[shader].count = 0;
    g_shader_ptrs[shader].needs_light_ssbo = 0;
    g_shader_ptrs[shader].needs_raw_ssbo = 0;
    char *p = out;
    char *end = out + cap - 1;
    int pos = 0;
    int ssbo = 30;
    int changed = 0;
    int eub[2] = {-1, -1}, eidx[2] = {-1, -1};
    int ecnt = 0;
    while (pos < len) {
        int bs, bo, bstart, bend, bend2, binding;
        if (!find_ubo_block(src, len, pos, &bs, &bo, &bstart, &bend, &bend2, &binding)) break;
        ecnt = 0;
        eub[0] = eub[1] = -1;
        eidx[0] = eidx[1] = -1;
        glog("RWT block bs=%d bo=%d bend=%d bend2=%d bind=%d\n", bs, bo, bend, bend2, binding);
        while (pos < bs && p < end) *p++ = src[pos++];
        int blen = bend - bstart;
        const char *body = src + bstart;
        ptr_member found[MAX_PTR_MEMBERS];
        int fms[MAX_PTR_MEMBERS], fme[MAX_PTR_MEMBERS];
        int fdrop[MAX_PTR_MEMBERS] = {0};
        int fcount = 0;
        int mp = 0;
        while (mp < blen && fcount < MAX_PTR_MEMBERS) {
            int ms, me;
            char t[64], nm[64];
            if (parse_pointer_member(body, blen, mp, &ms, &me, t, sizeof t, nm, sizeof nm)) {
                int bad = is_unsafe_type(t, unsafe, n_unsafe);
                glog("RWT   find[%d] ms=%d me=%d type=%s name=%s bad=%d\n",
                     fcount, ms, me, t, nm, bad);
                fdrop[fcount] = bad;
                if (!bad) {
                    found[fcount].ubo_binding = binding;
                    found[fcount].ssbo_binding = ssbo + fcount;
                    memset(found[fcount].name, 0, sizeof found[fcount].name);
                    memset(found[fcount].type, 0, sizeof found[fcount].type);
                    memcpy(found[fcount].name, nm, strlen(nm) < 63 ? strlen(nm) : 63);
                    memcpy(found[fcount].type, t, strlen(t) < 63 ? strlen(t) : 63);
                } else if (ecnt < 2) {
                    eub[ecnt] = binding;
                    eidx[ecnt] = fcount;
                    ecnt++;
                }
                fms[fcount] = ms;
                fme[fcount] = me;
                fcount++;
                mp = me;
            } else {
                mp++;
            }
        }
        /* copy header including '{' */
        if (fcount == 0) {
            while (pos < bend2 && p < end) *p++ = src[pos++];
        } else {
            /* re-scan to get absolute spans in declaration order */
            int scan = 0;
            char *nb = (char *)HeapAlloc(GetProcessHeap(), 0, (size_t)blen + 1);
            int nblen = 0;
            if (nb) {
                for (int fi = 0; fi < fcount; fi++) {
                    int rel = fms[fi];
                    while (scan < rel && nblen < blen) nb[nblen++] = body[scan++];
                    scan = fme[fi];
                }
                while (scan < blen && nblen < blen) nb[nblen++] = body[scan++];
                nb[nblen] = 0;
            }
            int blank = 1;
            for (int i = 0; i < nblen; i++) {
                char ch = nb[i];
                if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n') {
                    blank = 0;
                    break;
                }
            }
            if (!blank) {
                while (pos < bo + 1 && p < end) *p++ = src[pos++];
                for (int i = 0; i < nblen && p < end; i++) *p++ = nb[i];
                if (p < end) *p++ = '}';
                if (p < end) *p++ = ';';
                pos = bend2;
            } else {
                /* drop the whole now-empty UBO block */
                pos = bend2;
            }
            if (nb) HeapFree(GetProcessHeap(), 0, nb);
        }
        if (fcount > 0) {
            int emitted = 0;
            for (int fi = 0; fi < fcount; fi++) {
                if (fdrop[fi]) continue;
                found[fi].ssbo_binding = ssbo + emitted;
                int n = _snprintf(p, (size_t)(end - p),
                                  "\nlayout(std430, binding = %d) buffer PtrBuf_%d { %s %s[]; };",
                                  found[fi].ssbo_binding, found[fi].ssbo_binding - 30,
                                  found[fi].type, found[fi].name);
                if (n < 0) break;
                p += n;
                changed = 1;
                emitted++;
            }
            for (int fi = 0; fi < fcount && g_shader_ptrs[shader].count < MAX_PTR_MEMBERS; fi++) {
                if (fdrop[fi]) continue;
                g_shader_ptrs[shader].m[g_shader_ptrs[shader].count++] = found[fi];
            }
            ssbo += emitted;
        }
    }
    while (pos < len && p < end) *p++ = src[pos++];
    if (p < end) *p = 0;
    if (!changed) return -1;

    /* rewrite `name ->` into `name[0].` for rewritten pointer members */
    int olen = (int)(p - out);
    char *tmp = (char *)HeapAlloc(GetProcessHeap(), 0, (size_t)olen + 256);
    if (!tmp) return olen;
    char *q = tmp;
    const char *sp = out;
    int i = 0;
    while (i < olen) {
        int matched = 0;
        for (int fi = 0; fi < g_shader_ptrs[shader].count; fi++) {
            const char *nm = g_shader_ptrs[shader].m[fi].name;
            int nl = (int)strlen(nm);
            if (i + nl < olen && memcmp(sp + i, nm, (size_t)nl) == 0 &&
                (i == 0 || !is_namechar(sp[i - 1])) &&
                !is_namechar(sp[i + nl])) {
                int j = i + nl;
                while (j < olen && (sp[j] == ' ' || sp[j] == '\t')) j++;
                if (j + 1 < olen && sp[j] == '-' && sp[j + 1] == '>') {
                    memcpy(q, nm, (size_t)nl);
                    q += nl;
                    static const char arr[] = "[0].";
                    for (int k = 0; arr[k]; k++) *q++ = arr[k];
                    i = j + 2;
                    matched = 1;
                    break;
                }
            }
        }
        if (!matched) *q++ = sp[i++];
    }
    int nlen = (int)(q - tmp);
    if (nlen + 1 > cap) {
        HeapFree(GetProcessHeap(), 0, tmp);
        return olen;
    }
    memcpy(out, tmp, (size_t)nlen + 1);
    HeapFree(GetProcessHeap(), 0, tmp);

    /* BLEND_SHAPE: replace local NV pointer arithmetic with SSBO 29 access. */
    {
        int vcap = nlen + 2048;
        char *vout = (char *)HeapAlloc(GetProcessHeap(), 0, (size_t)vcap);
        if (vout) {
            int vlen = rewrite_vbo_pointers(shader, out, nlen, vout, vcap);
            if (vlen >= 0 && vlen + 1 <= cap) {
                memcpy(out, vout, (size_t)vlen + 1);
                nlen = vlen;
            }
            HeapFree(GetProcessHeap(), 0, vout);
        }
    }
    /* Rewrite used emitter struct pointers (g_unused0/g_unused1) into SSBOs. */
    {
        int ecap = nlen + 4096;
        char *eout = (char *)HeapAlloc(GetProcessHeap(), 0, (size_t)ecap);
        if (eout) {
            int elen = rewrite_emitter_struct_uses(shader, out, nlen, eout, ecap,
                                                   eub[0], eidx[0], eub[1], eidx[1]);
            if (elen >= 0 && elen + 1 <= cap) {
                memcpy(out, eout, (size_t)elen + 1);
                nlen = elen;
            }
            HeapFree(GetProcessHeap(), 0, eout);
        }
    }
    /* Remove struct definitions that still contain pointer members and are
       no longer referenced (their UBO members were dropped above). */
    {
        int scap = nlen + 2048;
        char *sout = (char *)HeapAlloc(GetProcessHeap(), 0, (size_t)scap);
        if (sout) {
            int slen = strip_unused_unsafe_structs(out, nlen, sout, scap);
            if (slen >= 0 && slen + 1 <= cap) {
                memcpy(out, sout, (size_t)slen + 1);
                nlen = slen;
            }
            HeapFree(GetProcessHeap(), 0, sout);
        }
    }
    /* Convert remaining NV pointer arithmetic (particle sim passes) into
       direct SSBO indexing. */
    {
        int acap = nlen + 4096;
        char *aout = (char *)HeapAlloc(GetProcessHeap(), 0, (size_t)acap);
        if (aout) {
            int alen = rewrite_particle_nv_arith(shader, out, nlen, aout, acap);
            if (alen >= 0 && alen + 1 <= cap) {
                memcpy(out, aout, (size_t)alen + 1);
                nlen = alen;
            }
            HeapFree(GetProcessHeap(), 0, aout);
        }
    }
    return nlen;
}

/* Bind SSBOs for the active program's rewritten pointer members by reading the
   UBO content and locating our fake GPU addresses. */
static glBindBufferRange_t real_glBindBufferRange;
/* Compile a throwaway shader to verify a rewritten source is valid before
   handing it to the game.  Returns 1 if the source compiles. */
static int test_compile(GLenum stype, const char *src, int len)
{
    if (!real_glCreateShader) real_glCreateShader = (glCreateShader_t)trace_resolve("glCreateShader");
    if (!real_glShaderSource) real_glShaderSource = (glShaderSource_t)trace_resolve("glShaderSource");
    if (!real_glCompileShader) real_glCompileShader = (glCompileShader_t)trace_resolve("glCompileShader");
    if (!real_glGetShaderiv) real_glGetShaderiv = (glGetShaderiv_t)trace_resolve("glGetShaderiv");
    if (!real_glDeleteShader) real_glDeleteShader = (glDeleteShader_t)trace_resolve("glDeleteShader");
    if (!real_glCreateShader || !real_glShaderSource || !real_glCompileShader ||
        !real_glGetShaderiv || !real_glDeleteShader) return 1;
    GLuint s = real_glCreateShader(stype);
    if (!s) return 1;
    real_glShaderSource(s, 1, &src, &len);
    real_glCompileShader(s);
    GLint ok = 0;
    real_glGetShaderiv(s, 0x8B81 /* GL_COMPILE_STATUS */, &ok);
    if (!ok && real_glGetShaderInfoLog) {
        int n = 0;
        real_glGetShaderInfoLog(s, 0, &n, NULL);
        if (n > 0 && n < 65536) {
            char *log = (char *)HeapAlloc(GetProcessHeap(), 0, (size_t)n + 1);
            if (log) {
                real_glGetShaderInfoLog(s, n, &n, log);
                char path[MAX_PATH];
                _snprintf(path, sizeof(path), "C:\\fgo\\_tools\\glshim\\testfail_%llu.log",
                          (unsigned long long)GetTickCount64());
                FILE *f = fopen(path, "w");
                if (f) { fwrite(log, 1, (size_t)n, f); fclose(f); }
                HeapFree(GetProcessHeap(), 0, log);
            }
        }
    }
    real_glDeleteShader(s);
    return ok != 0;
}

typedef struct {
    GLuint ubo_binding;
    GLuint ubo_buffer;
    GLintptr ubo_offset;
    GLsizeiptr ubo_size;
    int n;
    GLuint buf[MAX_PTR_MEMBERS];
    GLintptr off[MAX_PTR_MEMBERS];
    GLsizeiptr len[MAX_PTR_MEMBERS];
} ubo_scan_entry;

static ubo_scan_entry g_ubo_scan[16];
static int g_ubo_scan_n;
static int g_ssbo_dumped;

static void apply_pointer_bindings(void)
{
    GLuint prog = g_current_program;
    if (prog == 0 || prog >= 65536 || g_prog_ptrs[prog].count == 0) return;
    if (!real_glBindBufferRange)
        real_glBindBufferRange = (glBindBufferRange_t)trace_resolve("glBindBufferRange");
    if (!real_glGetNamedBufferSubData)
        real_glGetNamedBufferSubData = (glGetNamedBufferSubData_t)trace_resolve("glGetNamedBufferSubData");
    if (!real_glGetNamedBufferSubData || !real_glBindBufferRange) return;

    int count = g_prog_ptrs[prog].count;
    int gi = 0;
    while (gi < count) {
        int ub = g_prog_ptrs[prog].m[gi].ubo_binding;
        int ge = gi;
        while (ge < count && g_prog_ptrs[prog].m[ge].ubo_binding == ub) ge++;
        int group_n = ge - gi;
        if (ub >= 0 && ub < 64) {
            GLuint ubuf = g_ubo_buffer[ub];
            GLintptr uoff = (GLintptr)g_ubo_offset[ub];
            GLsizeiptr usize = (GLsizeiptr)g_ubo_length[ub];
            if (ubuf != 0 && usize > 0 && usize <= (1 << 20)) {
                ubo_scan_entry *ent = NULL;
                for (int ci = 0; ci < g_ubo_scan_n; ci++) {
                    if (g_ubo_scan[ci].ubo_binding == (GLuint)ub &&
                        g_ubo_scan[ci].ubo_buffer == ubuf &&
                        g_ubo_scan[ci].ubo_offset == uoff) {
                        ent = &g_ubo_scan[ci];
                        break;
                    }
                }
                if (!ent) {
                    unsigned char *tmp = (unsigned char *)HeapAlloc(GetProcessHeap(), 0, (size_t)usize);
                    if (tmp) {
                        real_glGetNamedBufferSubData(ubuf, uoff, usize, tmp);
                        int slot = g_ubo_scan_n;
                        if (slot >= 16) slot = 0;
                        ent = &g_ubo_scan[slot];
                        memset(ent, 0, sizeof *ent);
                        ent->ubo_binding = (GLuint)ub;
                        ent->ubo_buffer = ubuf;
                        ent->ubo_offset = uoff;
                        ent->ubo_size = usize;
                        int k = 0;
                        for (int o = 0; o + 8 <= usize && k < MAX_PTR_MEMBERS; o += 4) {
                            uint64_t v;
                            memcpy(&v, tmp + o, 8);
                            if (is_fake_addr(v)) {
                                GLuint b, off;
                                decode_fake_addr(v, &b, &off);
                                ent->buf[k] = b;
                                ent->off[k] = (GLintptr)off;
                                ent->len[k] = (b < (1u << 20) && g_buffer_size[b] > (GLsizeiptr)off)
                                                  ? (GLsizeiptr)(g_buffer_size[b] - off)
                                                  : 0;
                                k++;
                                o += 4;
                            }
                        }
                        ent->n = k;
                        if (slot == g_ubo_scan_n && g_ubo_scan_n < 16) g_ubo_scan_n++;
                        HeapFree(GetProcessHeap(), 0, tmp);
                    }
                }
                if (ent) {
                    for (int k = 0; k < group_n && k < ent->n && k < MAX_PTR_MEMBERS; k++) {
                        int ssbo = g_prog_ptrs[prog].m[gi + k].ssbo_binding;
                        if (ent->buf[k]) {
                            real_glBindBufferRange(GL_SHADER_STORAGE_BUFFER, (GLuint)ssbo,
                                                   ent->buf[k], ent->off[k], ent->len[k]);
                            glog("PTR prog=%u ssbo=%u <- buf=%u off=0x%llx len=%lld\n",
                                 prog, ssbo, ent->buf[k],
                                 (unsigned long long)ent->off[k],
                                 (long long)ent->len[k]);
                            if (ssbo == 30 && ent->len[k] >= 98304 && g_ssbo_dumped < 4 &&
                                real_glGetNamedBufferSubData) {
                                float pf[256];
                                memset(pf, 0, sizeof pf);
                                real_glGetNamedBufferSubData(ent->buf[k], ent->off[k],
                                                             sizeof pf, pf);
                                glog("PARTICLES prog=%u buf=%u off=%lld:", prog, ent->buf[k],
                                     (long long)ent->off[k]);
                                for (int q = 0; q < 256; q++) glog("%s%.3f", q ? "," : "", pf[q]);
                                glog("\n");
                                g_ssbo_dumped++;
                            }
                        }
                    }
                    if (g_prog_ptrs[prog].needs_emitter_count &&
                        (int)ub == g_prog_ptrs[prog].emitter_ubo_binding &&
                        ent->n > 0 && ent->buf[0] && ent->off[0] >= 8) {
                        real_glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 32, ent->buf[0],
                                               ent->off[0] - 8, 8);
                        glog("PTR prog=%u ssbo=32 <- buf=%u off=0x%llx len=8 (emitter count)\n",
                             prog, ent->buf[0], (unsigned long long)(ent->off[0] - 8));
                    }
                    for (int e = 0; e < g_prog_ptrs[prog].emitter_struct_count; e++) {
                        int idx = g_prog_ptrs[prog].emitter_struct_idx[e];
                        if ((int)ub != g_prog_ptrs[prog].emitter_struct_ubo[e]) continue;
                        if (idx < 0 || idx >= ent->n || !ent->buf[idx]) continue;
                        unsigned char st[16];
                        memset(st, 0, sizeof st);
                        real_glGetNamedBufferSubData(ent->buf[idx], ent->off[idx], sizeof st, st);
                        uint64_t a0, a1;
                        memcpy(&a0, st, 8);
                        memcpy(&a1, st + 8, 8);
                        if (is_fake_addr(a0)) {
                            GLuint b, off;
                            decode_fake_addr(a0, &b, &off);
                            GLsizeiptr len = (b < (1u << 20) && g_buffer_size[b] > (GLintptr)off)
                                                 ? (GLsizeiptr)(g_buffer_size[b] - off) : 0;
                            real_glBindBufferRange(GL_SHADER_STORAGE_BUFFER,
                                                   (GLuint)g_prog_ptrs[prog].emitter_struct_ssbo[e],
                                                   b, (GLintptr)off, len);
                            glog("PTR prog=%u ssbo=%d <- count buf=%u off=0x%x len=%lld\n",
                                 prog, g_prog_ptrs[prog].emitter_struct_ssbo[e], b, off,
                                 (long long)len);
                        }
                        if (is_fake_addr(a1)) {
                            GLuint b, off;
                            decode_fake_addr(a1, &b, &off);
                            GLsizeiptr len = (b < (1u << 20) && g_buffer_size[b] > (GLintptr)off)
                                                 ? (GLsizeiptr)(g_buffer_size[b] - off) : 0;
                            real_glBindBufferRange(GL_SHADER_STORAGE_BUFFER,
                                                   (GLuint)(g_prog_ptrs[prog].emitter_struct_ssbo[e] + 1),
                                                   b, (GLintptr)off, len);
                            glog("PTR prog=%u ssbo=%d <- indices buf=%u off=0x%x len=%lld\n",
                                 prog, g_prog_ptrs[prog].emitter_struct_ssbo[e] + 1, b, off,
                                 (long long)len);
                        }
                    }
                }
            }
        }
        gi = ge;
    }
}

void WINAPI glShaderSource_shim(unsigned int shader, int count, const char *const *string, const int *length)
{
    if (shader < 65536) {
        g_shader_ptrs[shader].count = 0;
        g_shader_ptrs[shader].needs_light_ssbo = 0;
        g_shader_ptrs[shader].needs_raw_ssbo = 0;
    }
    if (!real_glShaderSource) {
        if (real_wglGetProcAddress) {
            real_glShaderSource = (glShaderSource_t)real_wglGetProcAddress("glShaderSource");
        }
    }
    if (!real_glShaderSource) return;

    /* NVIDIA GLSL-pointer shaders cannot be fixed piece-by-piece: replace the
       whole source with an AMD-compilable stand-in. */
    {
        size_t joined_len = 0;
        for (int i = 0; i < count; i++) {
            const char *s = string[i];
            int len = length ? length[i] : (int)strlen(s);
            if (len < 0) len = (int)strlen(s);
            joined_len += (size_t)len;
        }
        char *joined = (char *)HeapAlloc(GetProcessHeap(), 0, joined_len ? joined_len : 1);
        if (joined) {
            size_t off = 0;
            for (int i = 0; i < count; i++) {
                const char *s = string[i];
                int len = length ? length[i] : (int)strlen(s);
                if (len < 0) len = (int)strlen(s);
                if (len > 0) {
                    memcpy(joined + off, s, (size_t)len);
                    off += (size_t)len;
                }
            }
            dump_shader_source(shader, "orig", joined, (int)off);
            if (has_nv_pointer(joined, (int)off)) {
                /* First try the pointer -> SSBO rewrite on the fixed source. */
                int fcap = (int)off + 4096 + 512;
                char *fixed = (char *)HeapAlloc(GetProcessHeap(), 0, (size_t)fcap);
                int rcap = fcap + 8192;
                char *rw = (char *)HeapAlloc(GetProcessHeap(), 0, (size_t)rcap);
                int rlen = -1;
                if (fixed && rw) {
                    int flen = fix_shader((shader < 65536) ? g_shader_types[shader] : 0,
                                          joined, (int)off, fixed, fcap);
                    const char *base = (flen >= 0) ? fixed : joined;
                    int blen = (flen >= 0) ? flen : (int)off;
                    if (flen >= 0) dump_shader_source(shader, "fixed", base, blen);
                    rlen = rewrite_pointer_shaders(shader, base, blen, rw, rcap);
                    if (rlen >= 0) {
                        GLenum stype = (shader < 65536) ? g_shader_types[shader] : 0;
                        if (test_compile(stype, rw, rlen)) {
                            store_shader_src(shader, rw, rlen);
                            if (shader < 65536) g_shader_replaced[shader] = 0;
                            log_shader_event("PTRREWRITE", shader, rw, rlen, stype);
                            dump_shader_source(shader, "ptrrewritten", rw, rlen);
                            real_glShaderSource(shader, 1, (const char *const *)&rw, &rlen);
                            HeapFree(GetProcessHeap(), 0, fixed);
                            HeapFree(GetProcessHeap(), 0, rw);
                            HeapFree(GetProcessHeap(), 0, joined);
                            return;
                        }
                        log_shader_event("PTRREJECT", shader, rw, rlen, stype);
                        dump_shader_source(shader, "ptrrejected", rw, rlen);
                    }
                    if (rlen < 0) {
                        /* No UBO pointer members: try local pointer rewrites
                           (BLEND_SHAPE packPtr / unsafe struct stripping). */
                        GLenum stype = (shader < 65536) ? g_shader_types[shader] : 0;
                        int r2 = rewrite_local_pointers(shader, base, blen, rw, rcap);
                        if (r2 >= 0 && test_compile(stype, rw, r2)) {
                            store_shader_src(shader, rw, r2);
                            if (shader < 65536) g_shader_replaced[shader] = 0;
                            log_shader_event("PTRREWRITE", shader, rw, r2, stype);
                            dump_shader_source(shader, "ptrrewritten", rw, r2);
                            real_glShaderSource(shader, 1, (const char *const *)&rw, &r2);
                            HeapFree(GetProcessHeap(), 0, fixed);
                            HeapFree(GetProcessHeap(), 0, rw);
                            HeapFree(GetProcessHeap(), 0, joined);
                            return;
                        }
                        if (r2 >= 0) {
                            log_shader_event("PTRREJECT", shader, rw, r2, stype);
                            dump_shader_source(shader, "ptrrejected", rw, r2);
                        }
                    }
                }
                if (fixed) HeapFree(GetProcessHeap(), 0, fixed);
                if (rw) HeapFree(GetProcessHeap(), 0, rw);
                /* Fall back to the previous whole-source no-op replacement. */
                char *buf = (char *)HeapAlloc(GetProcessHeap(), 0, 4096);
                if (buf) {
                    GLenum stype = (shader < 65536) ? g_shader_types[shader] : 0;
                    int r = make_nv_pointer_replacement(shader, stype, joined, (int)off, buf, 4096);
                    if (r >= 0) {
                        const char *one = buf;
                        int oneLen = r;
                        if (shader < 65536) g_shader_replaced[shader] = 1;
                        store_shader_src(shader, one, oneLen);
                        real_glShaderSource(shader, 1, &one, &oneLen);
                        HeapFree(GetProcessHeap(), 0, buf);
                        HeapFree(GetProcessHeap(), 0, joined);
                        return;
                    }
                    HeapFree(GetProcessHeap(), 0, buf);
                }
            }
            HeapFree(GetProcessHeap(), 0, joined);
        }
    }

    if (count <= 0 || count > 16) {
        real_glShaderSource(shader, count, string, length);
        return;
    }

    const char *strs[16];
    int lens[16];
    char *alloc[16] = {0};

    for (int i = 0; i < count; i++) {
        const char *s = string[i];
        int len = length ? length[i] : (int)strlen(s);
        if (len < 0) len = (int)strlen(s);
        char *buf = (char *)HeapAlloc(GetProcessHeap(), 0, (size_t)len + 4096);
        if (!buf) {
            strs[i] = s;
            lens[i] = len;
            continue;
        }
        int r = fix_shader((shader < 65536) ? g_shader_types[shader] : 0,
                           s, len, buf, len + 4095);
        if (r >= 0) {
            strs[i] = buf;
            lens[i] = r;
            alloc[i] = buf;
            GLenum stype = (shader < 65536) ? g_shader_types[shader] : 0;
            log_shader_event("REWRITE", shader, buf, r, stype);
            dump_shader_source(shader, "rewritten", buf, r);
        } else {
            strs[i] = s;
            lens[i] = len;
            HeapFree(GetProcessHeap(), 0, buf);
        }
    }

    real_glShaderSource(shader, count, strs, lens);

    {
        int total = 0;
        for (int i = 0; i < count; i++) total += lens[i];
        char *joined = (char *)HeapAlloc(GetProcessHeap(), 0, total ? (size_t)total : 1);
        if (joined) {
            int off = 0;
            for (int i = 0; i < count; i++) {
                memcpy(joined + off, strs[i], (size_t)lens[i]);
                off += lens[i];
            }
            store_shader_src(shader, joined, total);
            HeapFree(GetProcessHeap(), 0, joined);
        }
    }

    for (int i = 0; i < count; i++) {
        if (alloc[i]) HeapFree(GetProcessHeap(), 0, alloc[i]);
    }
    if (shader < 65536) g_shader_replaced[shader] = 0;
}

GLuint WINAPI glCreateShader_shim(GLenum type)
{
    GLuint id = 0;
    if (real_glCreateShader) id = real_glCreateShader(type);
    if (id && id < 65536) g_shader_types[id] = type;
    return id;
}

void WINAPI glGetShaderiv_shim(GLuint shader, GLenum pname, GLint *params)
{
    if (real_glGetShaderiv) real_glGetShaderiv(shader, pname, params);
    if (pname == 0x8B81 && params && *params == 0) {
        unsigned long long tick = (unsigned long long)GetTickCount64();
        char p[MAX_PATH];
        if (shader < 65536 && g_shader_src[shader].src) {
            _snprintf(p, sizeof(p), "C:\\fgo\\_tools\\glshim\\fail_shader_%u_%llu.glsl", shader, tick);
            FILE *f = fopen(p, "wb");
            if (f) {
                fwrite(g_shader_src[shader].src, 1, (size_t)g_shader_src[shader].len, f);
                fclose(f);
            }
        }
        if (real_glGetShaderInfoLog) {
            int n = 0;
            real_glGetShaderInfoLog(shader, 0, &n, NULL);
            if (n > 0 && n < 65536) {
                char *log = (char *)HeapAlloc(GetProcessHeap(), 0, (size_t)n + 1);
                if (log) {
                    real_glGetShaderInfoLog(shader, n, &n, log);
                    _snprintf(p, sizeof(p), "C:\\fgo\\_tools\\glshim\\fail_shader_%u_%llu.log", shader, tick);
                    FILE *f = fopen(p, "w");
                    if (f) { fwrite(log, 1, (size_t)n, f); fclose(f); }
                    HeapFree(GetProcessHeap(), 0, log);
                }
            }
        }
        GLenum stype = (shader < 65536) ? g_shader_types[shader] : 0;
        log_shader_event("COMPILE_FAIL", shader, g_shader_src[shader].src ? g_shader_src[shader].src : "",
                         shader < 65536 ? g_shader_src[shader].len : 0, stype);
    }
}

void WINAPI glGetProgramiv_shim(GLuint program, GLenum pname, GLint *params)
{
    if (real_glGetProgramiv) real_glGetProgramiv(program, pname, params);
    if (pname == 0x8B82 && params && *params == 0) {
        unsigned long long tick = (unsigned long long)GetTickCount64();
        char p[MAX_PATH];
        if (real_glGetProgramInfoLog) {
            int n = 0;
            real_glGetProgramInfoLog(program, 0, &n, NULL);
            if (n > 0 && n < 65536) {
                char *log = (char *)HeapAlloc(GetProcessHeap(), 0, (size_t)n + 1);
                if (log) {
                    real_glGetProgramInfoLog(program, n, &n, log);
                    _snprintf(p, sizeof(p), "C:\\fgo\\_tools\\glshim\\fail_program_%u_%llu.log", program, tick);
                    FILE *f = fopen(p, "w");
                    if (f) { fwrite(log, 1, (size_t)n, f); fclose(f); }
                    HeapFree(GetProcessHeap(), 0, log);
                }
            }
        }
        log_shader_event("LINK_FAIL", program, "", 0, 0);
    }
}

void WINAPI glDeleteShader_shim(GLuint shader)
{
    if (real_glDeleteShader) real_glDeleteShader(shader);
    if (shader < 65536) {
        if (g_shader_src[shader].src) HeapFree(GetProcessHeap(), 0, g_shader_src[shader].src);
        g_shader_src[shader].src = NULL;
        g_shader_src[shader].len = 0;
        g_shader_replaced[shader] = 0;
        g_shader_ptrs[shader].count = 0;
        g_shader_ptrs[shader].needs_light_ssbo = 0;
        g_shader_ptrs[shader].needs_raw_ssbo = 0;
    }
}


BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID reserved)
{
    (void)hinst; (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        /* Portable mode: diagnostics only run when this machine has the
           C:\fgo\_tools\glshim log dir; a clean client stays silent. */
        g_log_on = (GetFileAttributesA("C:\\fgo\\_tools\\glshim") != INVALID_FILE_ATTRIBUTES);
        g_capture_enabled = (GetFileAttributesA("C:\\fgo\\_tools\\glshim\\capture.on") != INVALID_FILE_ATTRIBUTES);
        g_bluefind_on = (GetFileAttributesA("C:\\fgo\\_tools\\glshim\\bluefind.on") != INVALID_FILE_ATTRIBUTES);
        g_real = load_real();
        if (g_real) {
            real_wglCreateContext = (wglCreateContext_t)GetProcAddress(g_real, "wglCreateContext");
            real_wglDeleteContext = (wglDeleteContext_t)GetProcAddress(g_real, "wglDeleteContext");
            real_wglMakeCurrent = (wglMakeCurrent_t)GetProcAddress(g_real, "wglMakeCurrent");
            real_wglGetProcAddress = (wglGetProcAddress_t)GetProcAddress(g_real, "wglGetProcAddress");
        }
        hook_gdi32_import();
    }
    return TRUE;
}

/* ---------- WGL_NV_DX_interop tracing ---------- */

typedef HANDLE (WINAPI *wglDXOpenDeviceNV_t)(void *);
typedef BOOL (WINAPI *wglDXCloseDeviceNV_t)(HANDLE);
typedef HANDLE (WINAPI *wglDXRegisterObjectNV_t)(HANDLE, void *, GLuint, GLenum, GLenum);
typedef BOOL (WINAPI *wglDXUnregisterObjectNV_t)(HANDLE, HANDLE);
typedef BOOL (WINAPI *wglDXLockObjectsNV_t)(HANDLE, GLint, HANDLE *);
typedef BOOL (WINAPI *wglDXUnlockObjectsNV_t)(HANDLE, GLint, HANDLE *);
typedef BOOL (WINAPI *wglDXSetResourceShareHandleNV_t)(void *, HANDLE);

static wglDXOpenDeviceNV_t real_wglDXOpenDeviceNV;
static wglDXCloseDeviceNV_t real_wglDXCloseDeviceNV;
static wglDXRegisterObjectNV_t real_wglDXRegisterObjectNV;
static wglDXUnregisterObjectNV_t real_wglDXUnregisterObjectNV;
static wglDXLockObjectsNV_t real_wglDXLockObjectsNV;
static wglDXUnlockObjectsNV_t real_wglDXUnlockObjectsNV;
static wglDXSetResourceShareHandleNV_t real_wglDXSetResourceShareHandleNV;
static int g_dx_fail_checked;
static int g_dx_fail;

static int dx_fail_mode(void)
{
    if (!g_dx_fail_checked) {
        g_dx_fail = (GetFileAttributesA("C:\\fgo\\_tools\\glshim\\dxfail.on")
                     != INVALID_FILE_ATTRIBUTES);
        g_dx_fail_checked = 1;
    }
    return g_dx_fail;
}

static void dxlog(const char *fn, const char *fmt, ...)
{
    va_list ap;
    char buf[512];
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    FILE *f = fopen("C:\\fgo\\_tools\\glshim\\dx.log", "a");
    if (f) {
        fprintf(f, "[%llu] %s %s\n", (unsigned long long)g_frame_count, fn, buf);
        fclose(f);
    }
}

static HANDLE WINAPI wrap_wglDXOpenDeviceNV(void *dxDevice)
{
    if (dx_fail_mode()) {
        dxlog("wglDXOpenDeviceNV", "dev=%p -> NULL (dxfail.on)", (unsigned long long)dxDevice);
        return NULL;
    }
    if (!real_wglDXOpenDeviceNV)
        real_wglDXOpenDeviceNV = (wglDXOpenDeviceNV_t)real_wglGetProcAddress("wglDXOpenDeviceNV");
    HANDLE r = real_wglDXOpenDeviceNV ? real_wglDXOpenDeviceNV(dxDevice) : NULL;
    dxlog("wglDXOpenDeviceNV", "dev=%p -> %p", (unsigned long long)dxDevice, (unsigned long long)r);
    return r;
}

static BOOL WINAPI wrap_wglDXCloseDeviceNV(HANDLE hDevice)
{
    if (!real_wglDXCloseDeviceNV)
        real_wglDXCloseDeviceNV = (wglDXCloseDeviceNV_t)real_wglGetProcAddress("wglDXCloseDeviceNV");
    BOOL r = real_wglDXCloseDeviceNV ? real_wglDXCloseDeviceNV(hDevice) : FALSE;
    dxlog("wglDXCloseDeviceNV", "dev=%p -> %d", (unsigned long long)hDevice, (unsigned long long)r, 0, 0);
    return r;
}

static HANDLE WINAPI wrap_wglDXRegisterObjectNV(HANDLE hDevice, void *dxObject,
                                                 GLuint name, GLenum type, GLenum access)
{
    if (dx_fail_mode()) {
        dxlog("wglDXRegisterObjectNV", "dev=%p obj=%p name=%u type=0x%x -> NULL (dxfail.on)",
              (unsigned long long)hDevice, (unsigned long long)dxObject, name, type);
        return NULL;
    }
    if (!real_wglDXRegisterObjectNV)
        real_wglDXRegisterObjectNV = (wglDXRegisterObjectNV_t)real_wglGetProcAddress("wglDXRegisterObjectNV");
    HANDLE r = real_wglDXRegisterObjectNV
                   ? real_wglDXRegisterObjectNV(hDevice, dxObject, name, type, access)
                   : NULL;
    dxlog("wglDXRegisterObjectNV", "dev=%p obj=%p name=%u type=0x%x access=0x%x -> %p",
          (unsigned long long)hDevice, (unsigned long long)dxObject, name, type, access,
          (unsigned long long)r);
    return r;
}

static BOOL WINAPI wrap_wglDXUnregisterObjectNV(HANDLE hDevice, HANDLE hObject)
{
    if (!real_wglDXUnregisterObjectNV)
        real_wglDXUnregisterObjectNV = (wglDXUnregisterObjectNV_t)real_wglGetProcAddress("wglDXUnregisterObjectNV");
    BOOL r = real_wglDXUnregisterObjectNV ? real_wglDXUnregisterObjectNV(hDevice, hObject) : FALSE;
    dxlog("wglDXUnregisterObjectNV", "dev=%p obj=%p -> %d", (unsigned long long)hDevice,
          (unsigned long long)hObject, (unsigned long long)r, 0);
    return r;
}

static BOOL WINAPI wrap_wglDXLockObjectsNV(HANDLE hDevice, GLint count, HANDLE *hObjects)
{
    if (!real_wglDXLockObjectsNV)
        real_wglDXLockObjectsNV = (wglDXLockObjectsNV_t)real_wglGetProcAddress("wglDXLockObjectsNV");
    BOOL r = real_wglDXLockObjectsNV ? real_wglDXLockObjectsNV(hDevice, count, hObjects) : FALSE;
    dxlog("wglDXLockObjectsNV", "dev=%p count=%d first=%p -> %d", (unsigned long long)hDevice,
          (unsigned long long)count, (unsigned long long)hObjects, (unsigned long long)r);
    return r;
}

static BOOL WINAPI wrap_wglDXUnlockObjectsNV(HANDLE hDevice, GLint count, HANDLE *hObjects)
{
    if (!real_wglDXUnlockObjectsNV)
        real_wglDXUnlockObjectsNV = (wglDXUnlockObjectsNV_t)real_wglGetProcAddress("wglDXUnlockObjectsNV");
    BOOL r = real_wglDXUnlockObjectsNV ? real_wglDXUnlockObjectsNV(hDevice, count, hObjects) : FALSE;
    dxlog("wglDXUnlockObjectsNV", "dev=%p count=%d first=%p -> %d", (unsigned long long)hDevice,
          (unsigned long long)count, (unsigned long long)hObjects, (unsigned long long)r);
    return r;
}

static BOOL WINAPI wrap_wglDXSetResourceShareHandleNV(void *dxObject, HANDLE shareHandle)
{
    if (!real_wglDXSetResourceShareHandleNV)
        real_wglDXSetResourceShareHandleNV = (wglDXSetResourceShareHandleNV_t)real_wglGetProcAddress("wglDXSetResourceShareHandleNV");
    BOOL r = real_wglDXSetResourceShareHandleNV
                 ? real_wglDXSetResourceShareHandleNV(dxObject, shareHandle)
                 : FALSE;
    dxlog("wglDXSetResourceShareHandleNV", "obj=%p share=%p -> %d", (unsigned long long)dxObject,
          (unsigned long long)shareHandle, (unsigned long long)r, 0);
    return r;
}

/* ---------- wglCreateContextAttribsARB tracing ---------- */
typedef HGLRC (WINAPI *wglCreateContextAttribsARB_t)(HDC, HGLRC, const int *);
static wglCreateContextAttribsARB_t real_wglCreateContextAttribsARB;

static HGLRC WINAPI wrap_wglCreateContextAttribsARB(HDC hdc, HGLRC share, const int *attribs)
{
    if (!real_wglCreateContextAttribsARB)
        real_wglCreateContextAttribsARB =
            (wglCreateContextAttribsARB_t)real_wglGetProcAddress("wglCreateContextAttribsARB");
    char abuf[512];
    abuf[0] = 0;
    if (attribs) {
        int off = 0;
        for (int i = 0; attribs[i] != 0 && off < 480; i += 2) {
            off += _snprintf(abuf + off, sizeof(abuf) - (size_t)off, "%s%d=%d",
                             i ? " " : "", attribs[i], attribs[i + 1]);
        }
    }
    HWND hw = WindowFromDC(hdc);
    RECT r = {0,0,0,0};
    if (hw) GetWindowRect(hw, &r);
    FILE *f = fopen("C:\\fgo\\_tools\\glshim\\swap.log", "a");
    if (f) {
        fprintf(f, "[%llu] wglCreateContextAttribsARB hdc=%p hwnd=%p rect=%d,%d,%d,%d pf=%d share=%p attribs={%s}\n",
                (unsigned long long)g_frame_count, (void *)hdc, (void *)hw,
                r.left, r.top, r.right, r.bottom, GetPixelFormat(hdc), (void *)share, abuf);
        fclose(f);
    }
    HGLRC c = real_wglCreateContextAttribsARB
                  ? real_wglCreateContextAttribsARB(hdc, share, attribs)
                  : NULL;
    f = fopen("C:\\fgo\\_tools\\glshim\\swap.log", "a");
    if (f) {
        fprintf(f, "[%llu] wglCreateContextAttribsARB -> ctx=%p err=%lu\n",
                (unsigned long long)g_frame_count, (void *)c, (unsigned long)GetLastError());
        fclose(f);
    }
    return c;
}

PROC WINAPI wglGetProcAddress_shim(LPCSTR name)
{
    PROC p;
    const char *arb;
    if (!name || !real_wglGetProcAddress) return NULL;
    {
        static int wgl_looked;
        if (wgl_looked < 6000) {
            wgl_looked++;
            FILE *f = fopen("C:\\fgo\\_tools\\glshim\\wgl_lookups.log", "a");
            if (f) {
                fprintf(f, "%s\n", name);
                fclose(f);
            }
        }
    }
    if (strcmp(name, "glShaderSource") == 0) return (PROC)glShaderSource_shim;
    if (strcmp(name, "glCreateShader") == 0) {
        if (!real_glCreateShader) real_glCreateShader = (glCreateShader_t)real_wglGetProcAddress(name);
        if (real_glCreateShader) return (PROC)glCreateShader_shim;
    }
    if (strcmp(name, "glGetShaderiv") == 0) {
        if (!real_glGetShaderiv) real_glGetShaderiv = (glGetShaderiv_t)real_wglGetProcAddress(name);
        if (real_glGetShaderiv) return (PROC)glGetShaderiv_shim;
    }
    if (strcmp(name, "glGetProgramiv") == 0) {
        if (!real_glGetProgramiv) real_glGetProgramiv = (glGetProgramiv_t)real_wglGetProcAddress(name);
        if (real_glGetProgramiv) return (PROC)glGetProgramiv_shim;
    }
    if (strcmp(name, "glGetShaderInfoLog") == 0) {
        if (!real_glGetShaderInfoLog) real_glGetShaderInfoLog = (glGetShaderInfoLog_t)real_wglGetProcAddress(name);
        if (real_glGetShaderInfoLog) return (PROC)real_glGetShaderInfoLog;
    }
    if (strcmp(name, "glGetProgramInfoLog") == 0) {
        if (!real_glGetProgramInfoLog) real_glGetProgramInfoLog = (glGetProgramInfoLog_t)real_wglGetProcAddress(name);
        if (real_glGetProgramInfoLog) return (PROC)real_glGetProgramInfoLog;
    }
    if (strcmp(name, "glDeleteShader") == 0) {
        if (!real_glDeleteShader) real_glDeleteShader = (glDeleteShader_t)real_wglGetProcAddress(name);
        if (real_glDeleteShader) return (PROC)glDeleteShader_shim;
    }
    if (strcmp(name, "wglDXOpenDeviceNV") == 0) return (PROC)wrap_wglDXOpenDeviceNV;
    if (strcmp(name, "wglDXCloseDeviceNV") == 0) return (PROC)wrap_wglDXCloseDeviceNV;
    if (strcmp(name, "wglDXRegisterObjectNV") == 0) return (PROC)wrap_wglDXRegisterObjectNV;
    if (strcmp(name, "wglDXUnregisterObjectNV") == 0) return (PROC)wrap_wglDXUnregisterObjectNV;
    if (strcmp(name, "wglDXLockObjectsNV") == 0) return (PROC)wrap_wglDXLockObjectsNV;
    if (strcmp(name, "wglDXUnlockObjectsNV") == 0) return (PROC)wrap_wglDXUnlockObjectsNV;
    if (strcmp(name, "wglDXSetResourceShareHandleNV") == 0) return (PROC)wrap_wglDXSetResourceShareHandleNV;
    if (strcmp(name, "wglCreateContextAttribsARB") == 0) {
        if (!real_wglCreateContextAttribsARB)
            real_wglCreateContextAttribsARB =
                (wglCreateContextAttribsARB_t)real_wglGetProcAddress(name);
        if (real_wglCreateContextAttribsARB) return (PROC)wrap_wglCreateContextAttribsARB;
    }
    p = find_hook(name);
    if (p) return p;
    p = real_wglGetProcAddress(name);
    if (p) return p;
    arb = nv_to_arb(name);
    if (arb) {
        p = real_wglGetProcAddress(arb);
        if (p) return p;
    }
    return find_stub(name);
}

HGLRC WINAPI wglCreateContext_shim(HDC hdc)
{
    return real_wglCreateContext ? real_wglCreateContext(hdc) : NULL;
}

static int g_swap_log_count;
typedef void (WINAPI *glGetIntegerv_t)(GLenum, GLint *);
static glGetIntegerv_t real_glGetIntegerv;

BOOL WINAPI wglMakeCurrent_shim(HDC hdc, HGLRC hglrc)
{
    if (g_swap_log_count < 400) {
        FILE *f = fopen("C:\\fgo\\_tools\\glshim\\swap.log", "a");
        if (f) {
            HWND hw = WindowFromDC(hdc);
            RECT r = {0,0,0,0};
            if (hw) GetWindowRect(hw, &r);
            fprintf(f, "[%llu] MakeCurrent hdc=%p hwnd=%p rect=%d,%d,%d,%d ctx=%p\n",
                    (unsigned long long)g_frame_count, (void *)hdc, (void *)hw,
                    r.left, r.top, r.right, r.bottom, (void *)hglrc);
            fclose(f);
        }
    }
    return real_wglMakeCurrent ? real_wglMakeCurrent(hdc, hglrc) : FALSE;
}

BOOL WINAPI wglDeleteContext_shim(HGLRC hglrc)
{
    return real_wglDeleteContext ? real_wglDeleteContext(hglrc) : FALSE;
}

static void swap_log_state(HDC hdc, const char *stage, BOOL result)
{
    if (g_swap_log_count >= 2000) return;
    if (g_swap_log_count >= 40 && (g_frame_count % 300) != 0) return;
    g_swap_log_count++;
    FILE *f = fopen("C:\\fgo\\_tools\\glshim\\swap.log", "a");
    if (!f) return;
    DWORD err = GetLastError();
    HWND hw = WindowFromDC(hdc);
    RECT r = {0,0,0,0};
    if (hw) GetWindowRect(hw, &r);
    int pf = GetPixelFormat(hdc);
    int vis = (hw && IsWindowVisible(hw)) ? 1 : 0;
    LONG_PTR style = hw ? GetWindowLongPtrW(hw, GWL_STYLE) : 0;
    LONG_PTR exstyle = hw ? GetWindowLongPtrW(hw, GWL_EXSTYLE) : 0;
    typedef HDC (WINAPI *wglGetCurrentDC_t)(void);
    typedef HGLRC (WINAPI *wglGetCurrentContext_t)(void);
    static wglGetCurrentDC_t curdc;
    static wglGetCurrentContext_t curctx;
    if (!curdc) curdc = (wglGetCurrentDC_t)GetProcAddress(g_real, "wglGetCurrentDC");
    if (!curctx) curctx = (wglGetCurrentContext_t)GetProcAddress(g_real, "wglGetCurrentContext");
    HDC cdc = curdc ? curdc() : NULL;
    HGLRC cctx = curctx ? curctx() : NULL;
    fprintf(f, "[%llu] SWAP %s hdc=%p hwnd=%p rect=%d,%d,%d,%d pf=%d vis=%d curdc=%p curctx=%p err=%lu result=%d\n",
            (unsigned long long)g_frame_count, stage, (void *)hdc, (void *)hw,
            r.left, r.top, r.right, r.bottom, pf, vis, (void *)cdc, (void *)cctx,
            err, (int)result);
    if (stage[0] == 'p' && g_swap_log_count < 60) {
        fprintf(f, "  style=0x%llx exstyle=0x%llx\n",
                (unsigned long long)style, (unsigned long long)exstyle);
        GLint draw_fbo = -1, read_fbo = -1, vp[4] = {0,0,0,0};
        if (real_glGetIntegerv) {
            real_glGetIntegerv(0x8CA9 /* GL_DRAW_FRAMEBUFFER_BINDING */, &draw_fbo);
            real_glGetIntegerv(0x8CAA /* GL_READ_FRAMEBUFFER_BINDING */, &read_fbo);
            real_glGetIntegerv(0x0BA2 /* GL_VIEWPORT */, vp);
        }
        fprintf(f, "  FBO draw=%d read=%d vp=(%d,%d,%d,%d)\n",
                draw_fbo, read_fbo, vp[0], vp[1], vp[2], vp[3]);
        PIXELFORMATDESCRIPTOR pfd;
        memset(&pfd, 0, sizeof(pfd));
        pfd.nSize = sizeof(pfd);
        if (DescribePixelFormat(hdc, pf, sizeof(pfd), &pfd)) {
            fprintf(f, "  PFD flags=0x%x type=%d cbits=%d depth=%d stencil=%d accum=%d layers=%d\n",
                    (unsigned)pfd.dwFlags, (int)pfd.iPixelType, (int)pfd.cColorBits,
                    (int)pfd.cDepthBits, (int)pfd.cStencilBits, (int)pfd.cAccumBits,
                    (int)pfd.bReserved);
        }
        static int gl_renderer_logged;
        if (!gl_renderer_logged) {
            gl_renderer_logged = 1;
            typedef const char *(WINAPI *glGetString_t)(GLenum);
            static glGetString_t gs;
            if (!gs) gs = (glGetString_t)(real_wglGetProcAddress ? real_wglGetProcAddress("glGetString") : NULL);
            if (gs) {
                const char *ren = gs(0x1F01 /* GL_RENDERER */);
                const char *ver = gs(0x1F02 /* GL_VERSION */);
                fprintf(f, "  GL_RENDERER=%s GL_VERSION=%s\n", ren ? ren : "?", ver ? ver : "?");
            }
        }
    }
    fclose(f);
}

/* ---------- framebuffer capture ---------- */

typedef BOOL (WINAPI *wglSwapBuffers_t)(HDC);
typedef void (WINAPI *glReadPixels_t)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *);
typedef void (WINAPI *glReadBuffer_t)(GLenum);
typedef void (WINAPI *glGetIntegerv_t)(GLenum, GLint *);
static wglSwapBuffers_t real_wglSwapBuffers;
static glReadPixels_t real_glReadPixels;
static glReadBuffer_t real_glReadBuffer;
static glGetIntegerv_t real_glGetIntegerv;

static void save_bmp(const char *path, int w, int h, const unsigned char *rgb)
{
    FILE *f = fopen(path, "wb");
    if (!f) return;
    int row = w * 3;
    int rowpad = (4 - (row % 4)) % 4;
    int data = (row + rowpad) * h;
    int file = 54 + data;
    unsigned char hdr[54];
    memset(hdr, 0, sizeof hdr);
    hdr[0] = 'B'; hdr[1] = 'M';
    hdr[2] = (unsigned char)(file & 0xff); hdr[3] = (unsigned char)((file >> 8) & 0xff);
    hdr[4] = (unsigned char)((file >> 16) & 0xff); hdr[5] = (unsigned char)((file >> 24) & 0xff);
    hdr[10] = 54;
    hdr[14] = 40;
    hdr[18] = (unsigned char)(w & 0xff); hdr[19] = (unsigned char)((w >> 8) & 0xff);
    hdr[20] = (unsigned char)((w >> 16) & 0xff); hdr[21] = (unsigned char)((w >> 24) & 0xff);
    hdr[22] = (unsigned char)(h & 0xff); hdr[23] = (unsigned char)((h >> 8) & 0xff);
    hdr[24] = (unsigned char)((h >> 16) & 0xff); hdr[25] = (unsigned char)((h >> 24) & 0xff);
    hdr[26] = 1;
    hdr[28] = 24;
    fwrite(hdr, 1, 54, f);
    for (int y = h - 1; y >= 0; y--) {
        const unsigned char *src = rgb + (size_t)y * w * 3;
        for (int x = 0; x < w; x++) {
            fputc(src[x * 3 + 2], f); /* B */
            fputc(src[x * 3 + 1], f); /* G */
            fputc(src[x * 3 + 0], f); /* R */
        }
        for (int p = 0; p < rowpad; p++) fputc(0, f);
    }
    fclose(f);
}

static void maybe_capture_frame(void)
{
    if (!g_log_on) return;
    if (!real_glReadPixels) real_glReadPixels = (glReadPixels_t)(real_wglGetProcAddress ? real_wglGetProcAddress("glReadPixels") : NULL);
    if (!real_glReadBuffer) real_glReadBuffer = (glReadBuffer_t)(real_wglGetProcAddress ? real_wglGetProcAddress("glReadBuffer") : NULL);
    if (!real_glGetIntegerv) real_glGetIntegerv = (glGetIntegerv_t)(real_wglGetProcAddress ? real_wglGetProcAddress("glGetIntegerv") : NULL);

    g_frame_count++;
    if (g_frame_count < 90) return;
    if (g_captures_written >= 24) return; /* limit IO so the game isn't starved */
    if (real_glReadPixels && real_glGetIntegerv && (g_frame_count % 300) == 0) {
        GLint vp[4] = {0};
        real_glGetIntegerv(0x0BA2 /* GL_VIEWPORT */, vp);
        int w = vp[2], h = vp[3];
        if (w > 0 && h > 0 && w <= 4096 && h <= 4096) {
            GLint prev_read = 0;
            if (real_glGetIntegerv) real_glGetIntegerv(0x0C02 /* GL_READ_BUFFER */, &prev_read);
            unsigned char *rgb = (unsigned char *)HeapAlloc(GetProcessHeap(), 0, (size_t)w * h * 3);
            if (rgb) {
                for (int pass = 0; pass < 2; pass++) {
                    GLenum buf = pass == 0 ? 0x0404 /* GL_FRONT */ : 0x0405 /* GL_BACK */;
                    if (real_glReadBuffer) real_glReadBuffer(buf);
                    memset(rgb, 0, (size_t)w * h * 3);
                    real_glReadPixels(0, 0, w, h, 0x1907 /* GL_RGB */, 0x1401 /* GL_UNSIGNED_BYTE */, rgb);
                    int nonzero = 0;
                    for (int i = 0; i < w * h * 3; i += 3) {
                        if (rgb[i] || rgb[i+1] || rgb[i+2]) { nonzero = 1; break; }
                    }
                    if (nonzero) {
                        char path[MAX_PATH];
                        _snprintf(path, sizeof(path), "C:\\fgo\\_tools\\glshim\\%s_%06llu_%dx%d.bmp",
                                  pass == 0 ? "front" : "back",
                                  (unsigned long long)g_frame_count, w, h);
                        save_bmp(path, w, h, rgb);
                        g_captures_written++;
                    }
                }
                if (real_glReadBuffer && prev_read != 0x0404 && prev_read != 0x0405) {
                    real_glReadBuffer((GLenum)prev_read);
                }
                HeapFree(GetProcessHeap(), 0, rgb);
            }
        }
    }
}

/* ---------- gdi32!SwapBuffers IAT hook (the game's real presentation path) ---------- */
typedef BOOL (WINAPI *gdi_SwapBuffers_t)(HDC);
typedef int (WINAPI *gdi_ChoosePixelFormat_t)(HDC, const void *);
typedef BOOL (WINAPI *gdi_SetPixelFormat_t)(HDC, int, const void *);
typedef BOOL (WINAPI *gdi_BitBlt_t)(HDC, int, int, int, int, HDC, int, int, DWORD);
typedef BOOL (WINAPI *gdi_StretchBlt_t)(HDC, int, int, int, int, HDC, int, int, int, int, DWORD);
typedef BOOL (WINAPI *gdi_StretchDIBits_t)(HDC, int, int, int, int, int, int, int, int, const void *, const void *, UINT, DWORD);
typedef int (WINAPI *gdi_SetDIBitsToDevice_t)(HDC, int, int, DWORD, DWORD, int, int, UINT, UINT, const void *, const void *, UINT);
static gdi_SwapBuffers_t real_gdi32_swapbuffers;
static gdi_ChoosePixelFormat_t real_gdi32_choosepf;
static gdi_SetPixelFormat_t real_gdi32_setpf;
static gdi_BitBlt_t real_gdi32_bitblt;
static gdi_StretchBlt_t real_gdi32_stretchblt;
static gdi_StretchDIBits_t real_gdi32_stretchdib;
static gdi_SetDIBitsToDevice_t real_gdi32_setdib;
static int g_gdi_swap_logged;
static int g_gdi_blt_logged;
static int g_pf_logged;
static int g_iat_hooked;

typedef HGLRC (WINAPI *ogl_wglCreateContext_t)(HDC);
typedef BOOL (WINAPI *ogl_wglMakeCurrent_t)(HDC, HGLRC);
typedef BOOL (WINAPI *ogl_wglDeleteContext_t)(HGLRC);
static ogl_wglCreateContext_t real_opengl32_createctx;
static ogl_wglMakeCurrent_t real_opengl32_makecurrent;
static ogl_wglDeleteContext_t real_opengl32_deletectx;
static int g_ctx_logged;
static void swap_dbg(const char *fmt, ...);

static HGLRC WINAPI hook_opengl32_wglCreateContext(HDC hdc)
{
    HWND hw = WindowFromDC(hdc);
    RECT r = {0,0,0,0};
    if (hw) GetWindowRect(hw, &r);
    swap_dbg("wglCreateContext hdc=%p hwnd=%p rect=%d,%d,%d,%d pf=%d",
             (void *)hdc, (void *)hw, r.left, r.top, r.right, r.bottom,
             GetPixelFormat(hdc));
    HGLRC c = real_opengl32_createctx ? real_opengl32_createctx(hdc) : NULL;
    swap_dbg("wglCreateContext -> ctx=%p err=%lu", (void *)c, (unsigned long)GetLastError());
    return c;
}

static BOOL WINAPI hook_opengl32_wglMakeCurrent(HDC hdc, HGLRC ctx)
{
    if (g_ctx_logged < 40) {
        g_ctx_logged++;
        HWND hw = WindowFromDC(hdc);
        RECT r = {0,0,0,0};
        if (hw) GetWindowRect(hw, &r);
        swap_dbg("wglMakeCurrent hdc=%p hwnd=%p rect=%d,%d,%d,%d pf=%d ctx=%p",
                 (void *)hdc, (void *)hw, r.left, r.top, r.right, r.bottom,
                 GetPixelFormat(hdc), (void *)ctx);
    }
    BOOL b = real_opengl32_makecurrent ? real_opengl32_makecurrent(hdc, ctx) : FALSE;
    if (g_ctx_logged < 40) {
        typedef HDC (WINAPI *getdc_t)(void);
        typedef HGLRC (WINAPI *getctx_t)(void);
        static getdc_t gd;
        static getctx_t gc;
        if (!gd) gd = (getdc_t)GetProcAddress(g_real, "wglGetCurrentDC");
        if (!gc) gc = (getctx_t)GetProcAddress(g_real, "wglGetCurrentContext");
        swap_dbg("wglMakeCurrent -> %d err=%lu curdc=%p curctx=%p", (int)b,
                 (unsigned long)GetLastError(),
                 (void *)(gd ? gd() : NULL), (void *)(gc ? gc() : NULL));
    }
    return b;
}

static BOOL WINAPI hook_opengl32_wglDeleteContext(HGLRC ctx)
{
    swap_dbg("wglDeleteContext ctx=%p", (void *)ctx);
    return real_opengl32_deletectx ? real_opengl32_deletectx(ctx) : FALSE;
}

static void swap_dbg(const char *fmt, ...)
{
    if (!g_log_on) return;
    va_list ap;
    char buf[512];
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    FILE *f = fopen("C:\\fgo\\_tools\\glshim\\swap.log", "a");
    if (f) {
        fprintf(f, "[%llu] %s\n", (unsigned long long)g_frame_count, buf);
        fclose(f);
    }
}

static int WINAPI hook_gdi32_ChoosePixelFormat(HDC hdc, const void *ppfd)
{
    hook_gdi32_import();
    int r = real_gdi32_choosepf ? real_gdi32_choosepf(hdc, ppfd) : 0;
    if (g_pf_logged < 20) {
        g_pf_logged++;
        if (ppfd) {
            const unsigned char *p = (const unsigned char *)ppfd;
            DWORD flags = *(const DWORD *)(p + 4);
            int ptype = *(const int *)(p + 8);
            int cbits = *(const int *)(p + 12);
            int depth = *(const int *)(p + 24);
            int stencil = *(const int *)(p + 28);
            swap_dbg("ChoosePixelFormat hdc=%p flags=0x%x type=%d colorbits=%d depth=%d stencil=%d -> %d",
                     (void *)hdc, (unsigned)flags, ptype, cbits, depth, stencil, r);
        } else {
            swap_dbg("ChoosePixelFormat hdc=%p pfd=NULL -> %d", (void *)hdc, r);
        }
    }
    return r;
}

static BOOL WINAPI hook_gdi32_SetPixelFormat(HDC hdc, int pf, const void *ppfd)
{
    BOOL r = real_gdi32_setpf ? real_gdi32_setpf(hdc, pf, ppfd) : FALSE;
    if (g_pf_logged < 20) {
        g_pf_logged++;
        if (ppfd) {
            const unsigned char *p = (const unsigned char *)ppfd;
            DWORD flags = *(const DWORD *)(p + 4);
            int ptype = *(const int *)(p + 8);
            int cbits = *(const int *)(p + 12);
            int depth = *(const int *)(p + 24);
            int stencil = *(const int *)(p + 28);
            swap_dbg("SetPixelFormat hdc=%p pf=%d flags=0x%x type=%d colorbits=%d depth=%d stencil=%d -> %d",
                     (void *)hdc, pf, (unsigned)flags, ptype, cbits, depth, stencil, (int)r);
        } else {
            swap_dbg("SetPixelFormat hdc=%p pf=%d pfd=NULL -> %d", (void *)hdc, pf, (int)r);
        }
    }
    return r;
}

static BOOL WINAPI hook_gdi32_BitBlt(HDC dst, int x, int y, int w, int h, HDC src, int sx, int sy, DWORD rop)
{
    if (g_gdi_blt_logged < 30) {
        g_gdi_blt_logged++;
        HWND hw = WindowFromDC(dst);
        swap_dbg("BitBlt dst=%p hwnd=%p rect=%d,%d,%dx%d src=%p srcxy=%d,%d rop=0x%x",
                 (void *)dst, (void *)hw, x, y, w, h, (void *)src, sx, sy, (unsigned)rop);
    }
    return real_gdi32_bitblt ? real_gdi32_bitblt(dst, x, y, w, h, src, sx, sy, rop) : FALSE;
}

static BOOL WINAPI hook_gdi32_StretchBlt(HDC dst, int x, int y, int w, int h, HDC src, int sx, int sy, int sw, int sh, DWORD rop)
{
    if (g_gdi_blt_logged < 30) {
        g_gdi_blt_logged++;
        HWND hw = WindowFromDC(dst);
        swap_dbg("StretchBlt dst=%p hwnd=%p rect=%d,%d,%dx%d src=%p src=%d,%d,%dx%d rop=0x%x",
                 (void *)dst, (void *)hw, x, y, w, h, (void *)src, sx, sy, sw, sh, (unsigned)rop);
    }
    return real_gdi32_stretchblt ? real_gdi32_stretchblt(dst, x, y, w, h, src, sx, sy, sw, sh, rop) : FALSE;
}

static int WINAPI hook_gdi32_StretchDIBits(HDC hdc, int x, int y, int w, int h,
                                           int sx, int sy, int sw, int sh,
                                           const void *bits, const void *bmi, UINT usage, DWORD rop)
{
    if (g_gdi_blt_logged < 30) {
        g_gdi_blt_logged++;
        HWND hw = WindowFromDC(hdc);
        swap_dbg("StretchDIBits hdc=%p hwnd=%p dst=%d,%d,%dx%d src=%d,%d,%dx%d bits=%p bmi=%p rop=0x%x",
                 (void *)hdc, (void *)hw, x, y, w, h, sx, sy, sw, sh, bits, bmi, (unsigned)rop);
    }
    return real_gdi32_stretchdib ? real_gdi32_stretchdib(hdc, x, y, w, h, sx, sy, sw, sh, bits, bmi, usage, rop) : 0;
}

static int WINAPI hook_gdi32_SetDIBitsToDevice(HDC hdc, int x, int y, DWORD w, DWORD h,
                                               int sx, int sy, UINT start, UINT lines,
                                               const void *bits, const void *bmi, UINT usage)
{
    if (g_gdi_blt_logged < 30) {
        g_gdi_blt_logged++;
        HWND hw = WindowFromDC(hdc);
        swap_dbg("SetDIBitsToDevice hdc=%p hwnd=%p pos=%d,%d size=%dx%d src=%d,%d lines=%u..%u bits=%p bmi=%p",
                 (void *)hdc, (void *)hw, x, y, (int)w, (int)h, sx, sy, start, lines, bits, bmi);
    }
    return real_gdi32_setdib ? real_gdi32_setdib(hdc, x, y, w, h, sx, sy, start, lines, bits, bmi, usage) : 0;
}

static BOOL WINAPI hook_gdi32_SwapBuffers(HDC hdc)
{
    BOOL r = real_gdi32_swapbuffers ? real_gdi32_swapbuffers(hdc) : FALSE;
#if 0
    /* pure passthrough when no diagnostics needed; set to 0 to enable logging */
    return r;
#endif
    if (g_gdi_swap_logged < 20 || (g_frame_count % 300) == 0) {
        g_gdi_swap_logged++;
        FILE *f = fopen("C:\\fgo\\_tools\\glshim\\swap.log", "a");
        if (f) {
            DWORD err = GetLastError();
            HWND hw = WindowFromDC(hdc);
            RECT rr = {0,0,0,0};
            if (hw) GetWindowRect(hw, &rr);
            fprintf(f, "[%llu] GDI-SwapBuffers hdc=%p hwnd=%p rect=%d,%d,%d,%d pf=%d err=%lu result=%d\n",
                    (unsigned long long)g_frame_count, (void *)hdc, (void *)hw,
                    rr.left, rr.top, rr.right, rr.bottom, GetPixelFormat(hdc), err, (int)r);
            fclose(f);
        }
    }
    return r;
}

static void hook_gdi32_import(void)
{
    if (g_iat_hooked) return;
    g_iat_hooked = 1;
    FILE *dbg = fopen("C:\\fgo\\_tools\\glshim\\swap.log", "a");
    if (dbg) {
        fprintf(dbg, "[0] IAT hook start real=%p\n", (void *)real_gdi32_swapbuffers);
        fclose(dbg);
    }
    HMODULE base = GetModuleHandleW(NULL);
    dbg = fopen("C:\\fgo\\_tools\\glshim\\swap.log", "a");
    if (dbg) {
        fprintf(dbg, "[0] IAT base=%p\n", (void *)base);
        fclose(dbg);
    }
    if (!base) return;
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)((BYTE *)base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return;
    IMAGE_DATA_DIRECTORY *dir =
        &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    IMAGE_IMPORT_DESCRIPTOR *imp =
        (IMAGE_IMPORT_DESCRIPTOR *)((BYTE *)base + dir->VirtualAddress);
    for (; imp->Name; imp++) {
        const char *name = (const char *)((BYTE *)base + imp->Name);
        int is_gdi = (_stricmp(name, "GDI32.dll") == 0);
        if (!is_gdi) continue;
        dbg = fopen("C:\\fgo\\_tools\\glshim\\swap.log", "a");
        if (dbg) {
            fprintf(dbg, "[0] IAT found %s oft=%p ft=%p\n", name,
                    (void *)((BYTE *)base + imp->OriginalFirstThunk),
                    (void *)((BYTE *)base + imp->FirstThunk));
            fclose(dbg);
        }
        IMAGE_THUNK_DATA *oft =
            (IMAGE_THUNK_DATA *)((BYTE *)base + imp->OriginalFirstThunk);
        IMAGE_THUNK_DATA *ft =
            (IMAGE_THUNK_DATA *)((BYTE *)base + imp->FirstThunk);
        int idx = 0;
        for (; oft->u1.AddressOfData; oft++, ft++) {
            idx++;
            if (!IMAGE_SNAP_BY_ORDINAL(oft->u1.Ordinal)) {
                IMAGE_IMPORT_BY_NAME *ibn =
                    (IMAGE_IMPORT_BY_NAME *)((BYTE *)base + oft->u1.AddressOfData);
                const char *fn = (const char *)ibn->Name;
                void *hook = NULL;
                if (strcmp(fn, "SwapBuffers") == 0) hook = (void *)hook_gdi32_SwapBuffers;
                else if (strcmp(fn, "ChoosePixelFormat") == 0) hook = (void *)hook_gdi32_ChoosePixelFormat;
                else if (strcmp(fn, "SetPixelFormat") == 0) hook = (void *)hook_gdi32_SetPixelFormat;
                else if (strcmp(fn, "BitBlt") == 0) hook = (void *)hook_gdi32_BitBlt;
                else if (strcmp(fn, "StretchBlt") == 0) hook = (void *)hook_gdi32_StretchBlt;
                else if (strcmp(fn, "StretchDIBits") == 0) hook = (void *)hook_gdi32_StretchDIBits;
                else if (strcmp(fn, "SetDIBitsToDevice") == 0) hook = (void *)hook_gdi32_SetDIBitsToDevice;
                if (hook) {
                    void **slot = (void **)&ft->u1.Function;
                    void *real = *slot;
                    dbg = fopen("C:\\fgo\\_tools\\glshim\\swap.log", "a");
                    if (dbg) {
                        fprintf(dbg, "[0] IAT hook %s real=%p\n", fn, real);
                        fclose(dbg);
                    }
                    if (strcmp(fn, "SwapBuffers") == 0) real_gdi32_swapbuffers = (gdi_SwapBuffers_t)real;
                    else if (strcmp(fn, "ChoosePixelFormat") == 0) real_gdi32_choosepf = (gdi_ChoosePixelFormat_t)real;
                    else if (strcmp(fn, "SetPixelFormat") == 0) real_gdi32_setpf = (gdi_SetPixelFormat_t)real;
                    else if (strcmp(fn, "BitBlt") == 0) real_gdi32_bitblt = (gdi_BitBlt_t)real;
                    else if (strcmp(fn, "StretchBlt") == 0) real_gdi32_stretchblt = (gdi_StretchBlt_t)real;
                    else if (strcmp(fn, "StretchDIBits") == 0) real_gdi32_stretchdib = (gdi_StretchDIBits_t)real;
                    else if (strcmp(fn, "SetDIBitsToDevice") == 0) real_gdi32_setdib = (gdi_SetDIBitsToDevice_t)real;
                    DWORD old;
                    if (VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &old)) {
                        *slot = hook;
                        VirtualProtect(slot, sizeof(void *), old, &old);
                    }
                }
            }
        }
    }
}

static void swap_front_probe(void);

static void copy_back_to_front(HDC hdc)
{
    typedef void (WINAPI *gdb_t)(GLenum);
    typedef void (WINAPI *grb_t)(GLenum);
    typedef void (WINAPI *gcp_t)(GLint, GLint, GLsizei, GLsizei, GLenum);
    typedef void (WINAPI *gbi_t)(GLenum, GLint *);
    static gdb_t gdb;
    static grb_t grb;
    static gcp_t gcp;
    static gbi_t gbi;
    if (!gdb) gdb = (gdb_t)trace_resolve("glDrawBuffer");
    if (!grb) grb = (grb_t)trace_resolve("glReadBuffer");
    if (!gcp) gcp = (gcp_t)trace_resolve("glCopyPixels");
    if (!gbi) gbi = (gbi_t)trace_resolve("glGetIntegerv");
    if (!gdb || !grb || !gcp || !gbi) return;
    GLint vp[4] = {0, 0, 0, 0};
    gbi(0x0BA2 /* GL_VIEWPORT */, vp);
    if (vp[2] <= 0 || vp[3] <= 0 || vp[2] > 8192 || vp[3] > 8192) return;
    gdb(0x0404 /* GL_FRONT */);
    grb(0x0405 /* GL_BACK */);
    gcp(0, 0, vp[2], vp[3], 0x1800 /* GL_COLOR */);
    gdb(0x0405);
    grb(0x0405);
    HWND hw = WindowFromDC(hdc);
    if (hw) {
        InvalidateRect(hw, NULL, FALSE);
        UpdateWindow(hw);
    }
}

static void black_dump(void)
{
    static int dumped;
    static unsigned long long cnt;
    if (dumped >= 40) return;
    if (GetFileAttributesA("C:\\fgo\\_tools\\glshim\\blackdump.on") == INVALID_FILE_ATTRIBUTES) return;
    cnt++;
    if (cnt % 300 != 0) return;
    dumped++;
    if (!real_glReadPixels) real_glReadPixels = (glReadPixels_t)(real_wglGetProcAddress ? real_wglGetProcAddress("glReadPixels") : NULL);
    if (!real_glReadBuffer) real_glReadBuffer = (glReadBuffer_t)(real_wglGetProcAddress ? real_wglGetProcAddress("glReadBuffer") : NULL);
    if (!real_glGetIntegerv) real_glGetIntegerv = (glGetIntegerv_t)(real_wglGetProcAddress ? real_wglGetProcAddress("glGetIntegerv") : NULL);
    GLint vp[4] = {0,0,0,0};
    if (real_glGetIntegerv) real_glGetIntegerv(0x0BA2, vp);
    int w = vp[2], h = vp[3];
    if (w <= 0 || h <= 0 || w > 8192 || h > 8192) w = 1920, h = 1080;
    unsigned char *buf = (unsigned char *)HeapAlloc(GetProcessHeap(), 0, (size_t)w * h * 3);
    if (!buf) return;
    memset(buf, 0, (size_t)w * h * 3);
    GLint prev_read = 0;
    if (real_glGetIntegerv) real_glGetIntegerv(0x0C02, &prev_read);
    if (real_glReadBuffer) real_glReadBuffer(0x0404 /* GL_FRONT */);
    if (real_glReadPixels) real_glReadPixels(0, 0, w, h, 0x1907, 0x1401, buf);
    int fnonzero = 0;
    for (int i = 0; i < w * h * 3; i += 3)
        if (buf[i] || buf[i+1] || buf[i+2]) { fnonzero = 1; break; }
    if (fnonzero) {
        char fpath[MAX_PATH];
        _snprintf(fpath, sizeof(fpath), "C:\\fgo\\_tools\\glshim\\front_%llu_%dx%d.bmp",
                  (unsigned long long)cnt, w, h);
        save_bmp(fpath, w, h, buf);
    }
    if (real_glReadBuffer) real_glReadBuffer(0x0405);
    if (real_glReadPixels) real_glReadPixels(0, 0, w, h, 0x1907, 0x1401, buf);
    if (real_glReadBuffer && prev_read != 0x0404 && prev_read != 0x0405) real_glReadBuffer((GLenum)prev_read);
    char path[MAX_PATH];
    _snprintf(path, sizeof(path), "C:\\fgo\\_tools\\glshim\\black_%llu_%dx%d.bmp",
              (unsigned long long)cnt, w, h);
    save_bmp(path, w, h, buf);
    HeapFree(GetProcessHeap(), 0, buf);
    dump_tex_image_tag(0x0DE1, 120, "black");
    dump_tex_image_tag(0x0DE1, 96, "black");
    {
        static const GLuint chain[] = {20, 24, 25, 26, 95, 96, 98, 99, 101, 104, 106, 107, 109, 110, 111, 118, 120};
        for (size_t ci = 0; ci < sizeof(chain) / sizeof(chain[0]); ci++)
            dump_tex_image_tag(0x0DE1, chain[ci], "black");
    }
    FILE *f = fopen("C:\\fgo\\_tools\\glshim\\swap.log", "a");
    if (f) {
        fprintf(f, "[%llu] BLACKDUMP saved=%s prog=%u\n",
                (unsigned long long)g_frame_count, path, g_current_program);
        fclose(f);
    }
}

BOOL WINAPI wglSwapBuffers_shim(HDC hdc)
{
    /* Root cause (2026-08-14): the shim loads two instances of the real
       opengl32.dll -- opengl32real.dll (all forwarded GL exports) and the
       System32 copy used as g_real.  Calling wglSwapBuffers through the
       System32 instance fails with ERROR_ACCESS_DENIED / INVALID_HANDLE.
       The old 16:21 shim forwarded wglSwapBuffers straight to
       opengl32real.wglSwapBuffers, so resolve the real function from
       opengl32real.dll first. */
    if (!real_wglSwapBuffers) {
        HMODULE hreal = GetModuleHandleA("opengl32real.dll");
        if (hreal) real_wglSwapBuffers = (wglSwapBuffers_t)GetProcAddress(hreal, "wglSwapBuffers");
        if (!real_wglSwapBuffers) real_wglSwapBuffers = (wglSwapBuffers_t)GetProcAddress(g_real, "wglSwapBuffers");
    }
    /* minimal forward: no readbacks, no GL state queries, no file IO before swap */
    swap_log_state(hdc, "pre", FALSE);
    BOOL r = real_wglSwapBuffers ? real_wglSwapBuffers(hdc) : FALSE;
    swap_log_state(hdc, "post", r);
#if 0
    /* VARIANT D: copyfix disabled so the front buffer keeps whatever the UI
       pass draws (old shim behavior). */
    if (!r) {
        copy_back_to_front(hdc);
        static int copy_logged;
        if (copy_logged < 20) {
            copy_logged++;
            FILE *f = fopen("C:\\fgo\\_tools\\glshim\\swap.log", "a");
            if (f) {
                fprintf(f, "[%llu] SWAP failed, back->front copy attempted\n",
                        (unsigned long long)g_frame_count);
                fclose(f);
            }
        }
    }
#endif
    black_dump();
    swap_front_probe();
    static unsigned long long swap_calls;
    swap_calls++;
    if (swap_calls <= 30 || (swap_calls % 300) == 0) {
        FILE *f = fopen("C:\\fgo\\_tools\\glshim\\swap.log", "a");
        if (f) {
            fprintf(f, "[%llu] MINSWAP call=%llu result=%d err=%lu\n",
                    (unsigned long long)g_frame_count, swap_calls, (int)r,
                    (unsigned long)GetLastError());
            fclose(f);
        }
    }
    return r;
}

static void swap_front_probe(void)
{
    if (!real_glReadPixels) real_glReadPixels = (glReadPixels_t)(real_wglGetProcAddress ? real_wglGetProcAddress("glReadPixels") : NULL);
    if (!real_glReadBuffer) real_glReadBuffer = (glReadBuffer_t)(real_wglGetProcAddress ? real_wglGetProcAddress("glReadBuffer") : NULL);
    if (!real_glReadPixels || !real_glReadBuffer) return;
    if (g_swap_log_count >= 2000) return;
    enum { PW = 64, PH = 36 };
    unsigned char buf[PW * PH * 3];
    memset(buf, 0, sizeof(buf));
    GLint prev_read = 0;
    if (real_glGetIntegerv) real_glGetIntegerv(0x0C02, &prev_read);
    real_glReadBuffer(0x0404 /* GL_FRONT */);
    real_glReadPixels(0, 0, PW, PH, 0x1907, 0x1401, buf);
    if (prev_read != 0x0404 && prev_read != 0x0405 && prev_read != 0) real_glReadBuffer((GLenum)prev_read);
    long long sum[3] = {0,0,0};
    int nonzero = 0, white = 0, blue = 0;
    for (int i = 0; i < PW * PH; i++) {
        unsigned char r = buf[i*3+0], g = buf[i*3+1], b = buf[i*3+2];
        sum[0] += r; sum[1] += g; sum[2] += b;
        if (r || g || b) nonzero++;
        if (r > 235 && g > 235 && b > 235) white++;
        if (b > 80 && b > r + 40 && b > g + 40) blue++;
    }
    FILE *f = fopen("C:\\fgo\\_tools\\glshim\\swap.log", "a");
    if (f) {
        fprintf(f, "[%llu] FRONT-AFTER-SWAP avg=(%lld,%lld,%lld) nonzero=%d/%d white=%d blue=%d\n",
                (unsigned long long)g_frame_count,
                sum[0]/(PW*PH), sum[1]/(PW*PH), sum[2]/(PW*PH),
                nonzero, PW*PH, white, blue);
        fclose(f);
    }
}

typedef BOOL (WINAPI *wglSetPixelFormat_t)(HDC, int, const void *);
typedef int (WINAPI *wglChoosePixelFormat_t)(HDC, const void *);
typedef int (WINAPI *wglDescribePixelFormat_t)(HDC, int, unsigned int, void *);
typedef PROC (WINAPI *wglGetDefaultProcAddress_t)(LPCSTR);
static wglSetPixelFormat_t real_wglSetPixelFormat;
static wglChoosePixelFormat_t real_wglChoosePixelFormat;
static wglDescribePixelFormat_t real_wglDescribePixelFormat;
static wglGetDefaultProcAddress_t real_wglGetDefaultProcAddress;

BOOL WINAPI wglSetPixelFormat_shim(HDC hdc, int iPixelFormat, const void *ppfd)
{
    if (!real_wglSetPixelFormat) real_wglSetPixelFormat = (wglSetPixelFormat_t)GetProcAddress(g_real, "wglSetPixelFormat");
    if (g_swap_log_count < 400) {
        FILE *f = fopen("C:\\fgo\\_tools\\glshim\\swap.log", "a");
        if (f) {
            HWND hw = WindowFromDC(hdc);
            RECT r = {0,0,0,0};
            if (hw) GetWindowRect(hw, &r);
            fprintf(f, "[%llu] SetPixelFormat hdc=%p hwnd=%p rect=%d,%d,%d,%d pf=%d\n",
                    (unsigned long long)g_frame_count, (void *)hdc, (void *)hw,
                    r.left, r.top, r.right, r.bottom, iPixelFormat);
            fclose(f);
        }
    }
    BOOL r = real_wglSetPixelFormat ? real_wglSetPixelFormat(hdc, iPixelFormat, ppfd) : FALSE;
    if (!r && g_swap_log_count < 400) {
        FILE *f = fopen("C:\\fgo\\_tools\\glshim\\swap.log", "a");
        if (f) {
            fprintf(f, "[%llu] SetPixelFormat FAILED hdc=%p pf=%d err=%lu\n",
                    (unsigned long long)g_frame_count, (void *)hdc, iPixelFormat, (unsigned long)GetLastError());
            fclose(f);
        }
    }
    return r;
}

int WINAPI wglChoosePixelFormat_shim(HDC hdc, const void *ppfd)
{
    if (!real_wglChoosePixelFormat) real_wglChoosePixelFormat = (wglChoosePixelFormat_t)GetProcAddress(g_real, "wglChoosePixelFormat");
    int r = real_wglChoosePixelFormat ? real_wglChoosePixelFormat(hdc, ppfd) : 0;
    if (g_swap_log_count < 400) {
        FILE *f = fopen("C:\\fgo\\_tools\\glshim\\swap.log", "a");
        if (f) {
            HWND hw = WindowFromDC(hdc);
            RECT rr = {0,0,0,0};
            if (hw) GetWindowRect(hw, &rr);
            fprintf(f, "[%llu] ChoosePixelFormat hdc=%p hwnd=%p rect=%d,%d,%d,%d -> %d\n",
                    (unsigned long long)g_frame_count, (void *)hdc, (void *)hw,
                    rr.left, rr.top, rr.right, rr.bottom, r);
            fclose(f);
        }
    }
    return r;
}

PROC WINAPI wglGetDefaultProcAddress_shim(LPCSTR name)
{
    if (!real_wglGetDefaultProcAddress)
        real_wglGetDefaultProcAddress = (wglGetDefaultProcAddress_t)GetProcAddress(g_real, "wglGetDefaultProcAddress");
    PROC r = real_wglGetDefaultProcAddress ? real_wglGetDefaultProcAddress(name) : NULL;
    if (name && (strncmp(name, "wglDX", 5) == 0 || g_swap_log_count < 400)) {
        FILE *f = fopen("C:\\fgo\\_tools\\glshim\\swap.log", "a");
        if (f) {
            fprintf(f, "[%llu] GetDefaultProcAddress %s -> %p\n",
                    (unsigned long long)g_frame_count, name, (void *)r);
            fclose(f);
        }
    }
    return r;
}
