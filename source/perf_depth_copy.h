/* Same-frame depth format conversion without a CPU round trip. */
static int g_perf_depth_copy_gpu_on;
static struct {
    HGLRC context;
    GLuint program, vao, fbo, sampler;
    int failed;
} g_perf_depth_gpu;
static struct {
    HGLRC (WINAPI *context)(void);
    void (WINAPI *get_i)(GLenum, GLint *);
    void (WINAPI *get_b)(GLenum, GLboolean *);
    void (WINAPI *get_fi)(GLenum, GLuint, float *);
    void (WINAPI *get_di)(GLenum, GLuint, double *);
    void (WINAPI *get_query)(GLenum, GLenum, GLint *);
    void (WINAPI *get_tex)(GLuint, GLenum, GLint *);
    void (WINAPI *get_level)(GLuint, GLint, GLenum, GLint *);
    GLenum (WINAPI *error)(void);
    GLboolean (WINAPI *enabled)(GLenum);
    GLboolean (WINAPI *enabled_i)(GLenum, GLuint);
    void (WINAPI *enable)(GLenum);
    void (WINAPI *disable)(GLenum);
    void (WINAPI *enable_i)(GLenum, GLuint);
    void (WINAPI *disable_i)(GLenum, GLuint);
    GLuint (WINAPI *create_shader)(GLenum);
    void (WINAPI *shader_source)(GLuint, GLsizei, const char *const *, const GLint *);
    void (WINAPI *compile)(GLuint);
    void (WINAPI *shader_i)(GLuint, GLenum, GLint *);
    void (WINAPI *delete_shader)(GLuint);
    GLuint (WINAPI *create_program)(void);
    void (WINAPI *attach)(GLuint, GLuint);
    void (WINAPI *link)(GLuint);
    void (WINAPI *program_i)(GLuint, GLenum, GLint *);
    void (WINAPI *delete_program)(GLuint);
    void (WINAPI *use)(GLuint);
    void (WINAPI *uniform2i)(GLuint, GLint, GLint, GLint);
    void (WINAPI *gen_vaos)(GLsizei, GLuint *);
    void (WINAPI *bind_vao)(GLuint);
    void (WINAPI *delete_vaos)(GLsizei, const GLuint *);
    void (WINAPI *create_fbos)(GLsizei, GLuint *);
    void (WINAPI *fbo_texture)(GLuint, GLenum, GLuint, GLint);
    void (WINAPI *fbo_attachment)(GLuint, GLenum, GLenum, GLint *);
    void (WINAPI *fbo_draw)(GLuint, GLenum);
    void (WINAPI *fbo_read)(GLuint, GLenum);
    GLenum (WINAPI *fbo_status)(GLuint, GLenum);
    void (WINAPI *bind_fbo)(GLenum, GLuint);
    void (WINAPI *delete_fbos)(GLsizei, const GLuint *);
    void (WINAPI *create_samplers)(GLsizei, GLuint *);
    void (WINAPI *sampler_i)(GLuint, GLenum, GLint);
    void (WINAPI *bind_sampler)(GLuint, GLuint);
    void (WINAPI *delete_samplers)(GLsizei, const GLuint *);
    void (WINAPI *active_texture)(GLenum);
    void (WINAPI *bind_texture)(GLenum, GLuint);
    void (WINAPI *viewport)(GLuint, float, float, float, float);
    void (WINAPI *depth_range)(GLuint, double, double);
    void (WINAPI *depth_func)(GLenum);
    void (WINAPI *depth_mask)(GLboolean);
    void (WINAPI *polygon_mode)(GLenum, GLenum);
    void (WINAPI *clip_control)(GLenum, GLenum);
    void (WINAPI *draw)(GLenum, GLint, GLsizei);
    int resolved;
} g_depth_gpu_api;

static int perf_depth_gpu_resolve(void)
{
    if (g_depth_gpu_api.resolved) return g_depth_gpu_api.resolved > 0;
    g_depth_gpu_api.resolved = -1;
    HMODULE owner = GetModuleHandleA("opengl32real.dll");
    if (!owner) owner = g_real;
#define DEPTH_PROC(member, name) do { \
    g_depth_gpu_api.member = (__typeof__(g_depth_gpu_api.member))trace_resolve(name); \
    if (!g_depth_gpu_api.member && owner) \
        g_depth_gpu_api.member = (__typeof__(g_depth_gpu_api.member))GetProcAddress(owner,name); \
    if (!g_depth_gpu_api.member) return 0; \
} while (0)
    DEPTH_PROC(context, "wglGetCurrentContext");
    DEPTH_PROC(get_i, "glGetIntegerv");
    DEPTH_PROC(get_b, "glGetBooleanv");
    DEPTH_PROC(get_fi, "glGetFloati_v");
    DEPTH_PROC(get_di, "glGetDoublei_v");
    DEPTH_PROC(get_query, "glGetQueryiv");
    DEPTH_PROC(get_tex, "glGetTextureParameteriv");
    DEPTH_PROC(get_level, "glGetTextureLevelParameteriv");
    DEPTH_PROC(error, "glGetError");
    DEPTH_PROC(enabled, "glIsEnabled");
    DEPTH_PROC(enabled_i, "glIsEnabledi");
    DEPTH_PROC(enable, "glEnable");
    DEPTH_PROC(disable, "glDisable");
    DEPTH_PROC(enable_i, "glEnablei");
    DEPTH_PROC(disable_i, "glDisablei");
    DEPTH_PROC(create_shader, "glCreateShader");
    DEPTH_PROC(shader_source, "glShaderSource");
    DEPTH_PROC(compile, "glCompileShader");
    DEPTH_PROC(shader_i, "glGetShaderiv");
    DEPTH_PROC(delete_shader, "glDeleteShader");
    DEPTH_PROC(create_program, "glCreateProgram");
    DEPTH_PROC(attach, "glAttachShader");
    DEPTH_PROC(link, "glLinkProgram");
    DEPTH_PROC(program_i, "glGetProgramiv");
    DEPTH_PROC(delete_program, "glDeleteProgram");
    DEPTH_PROC(use, "glUseProgram");
    DEPTH_PROC(uniform2i, "glProgramUniform2i");
    DEPTH_PROC(gen_vaos, "glGenVertexArrays");
    DEPTH_PROC(bind_vao, "glBindVertexArray");
    DEPTH_PROC(delete_vaos, "glDeleteVertexArrays");
    DEPTH_PROC(create_fbos, "glCreateFramebuffers");
    DEPTH_PROC(fbo_texture, "glNamedFramebufferTexture");
    DEPTH_PROC(fbo_attachment, "glGetNamedFramebufferAttachmentParameteriv");
    DEPTH_PROC(fbo_draw, "glNamedFramebufferDrawBuffer");
    DEPTH_PROC(fbo_read, "glNamedFramebufferReadBuffer");
    DEPTH_PROC(fbo_status, "glCheckNamedFramebufferStatus");
    DEPTH_PROC(bind_fbo, "glBindFramebuffer");
    DEPTH_PROC(delete_fbos, "glDeleteFramebuffers");
    DEPTH_PROC(create_samplers, "glCreateSamplers");
    DEPTH_PROC(sampler_i, "glSamplerParameteri");
    DEPTH_PROC(bind_sampler, "glBindSampler");
    DEPTH_PROC(delete_samplers, "glDeleteSamplers");
    DEPTH_PROC(active_texture, "glActiveTexture");
    DEPTH_PROC(bind_texture, "glBindTexture");
    DEPTH_PROC(viewport, "glViewportIndexedf");
    DEPTH_PROC(depth_range, "glDepthRangeIndexed");
    DEPTH_PROC(depth_func, "glDepthFunc");
    DEPTH_PROC(depth_mask, "glDepthMask");
    DEPTH_PROC(polygon_mode, "glPolygonMode");
    DEPTH_PROC(clip_control, "glClipControl");
    DEPTH_PROC(draw, "glDrawArrays");
#undef DEPTH_PROC
    g_depth_gpu_api.resolved = 1;
    return 1;
}

static int perf_depth_gpu_prepare(HGLRC context)
{
    if (g_perf_depth_gpu.context && g_perf_depth_gpu.context != context) return 0;
    if (g_perf_depth_gpu.failed) return 0;
    if (g_perf_depth_gpu.program) return 1;
    g_perf_depth_gpu.context = context;
    const char *sources[] = {
        "#version 450 core\n"
        "void main(){vec2 p[3]=vec2[3](vec2(-1,-1),vec2(3,-1),vec2(-1,3));"
        "gl_Position=vec4(p[gl_VertexID],0,1);}\n",
        "#version 450 core\n"
        "layout(binding=0) uniform sampler2D depth_source;\n"
        "layout(location=0) uniform ivec2 source_delta;\n"
        "void main(){gl_FragDepth=texelFetch(depth_source,"
        "ivec2(gl_FragCoord.xy)+source_delta,0).r;}\n"
    };
    GLuint shaders[2] = {0,0};
    int ok = 1;
    g_perf_depth_gpu.program = g_depth_gpu_api.create_program();
    for (int i = 0; i < 2 && ok; i++) {
        GLint compiled = 0;
        shaders[i] = g_depth_gpu_api.create_shader(i ? 0x8B30 : 0x8B31);
        g_depth_gpu_api.shader_source(shaders[i], 1, &sources[i], NULL);
        g_depth_gpu_api.compile(shaders[i]);
        g_depth_gpu_api.shader_i(shaders[i], 0x8B81, &compiled);
        ok = compiled != 0;
        if (ok) g_depth_gpu_api.attach(g_perf_depth_gpu.program, shaders[i]);
    }
    if (ok) {
        GLint linked = 0;
        g_depth_gpu_api.link(g_perf_depth_gpu.program);
        g_depth_gpu_api.program_i(g_perf_depth_gpu.program, 0x8B82, &linked);
        ok = linked != 0;
    }
    for (int i = 0; i < 2; i++) if (shaders[i]) g_depth_gpu_api.delete_shader(shaders[i]);
    if (ok) {
        g_depth_gpu_api.gen_vaos(1, &g_perf_depth_gpu.vao);
        g_depth_gpu_api.create_fbos(1, &g_perf_depth_gpu.fbo);
        g_depth_gpu_api.fbo_draw(g_perf_depth_gpu.fbo, 0);
        g_depth_gpu_api.fbo_read(g_perf_depth_gpu.fbo, 0);
        g_depth_gpu_api.create_samplers(1, &g_perf_depth_gpu.sampler);
        g_depth_gpu_api.sampler_i(g_perf_depth_gpu.sampler, 0x884C, 0);
        g_depth_gpu_api.sampler_i(g_perf_depth_gpu.sampler, 0x2801, 0x2600);
        g_depth_gpu_api.sampler_i(g_perf_depth_gpu.sampler, 0x2800, 0x2600);
        ok = g_depth_gpu_api.error() == 0;
    }
    if (!ok) {
        if (g_perf_depth_gpu.program) g_depth_gpu_api.delete_program(g_perf_depth_gpu.program);
        if (g_perf_depth_gpu.vao) g_depth_gpu_api.delete_vaos(1, &g_perf_depth_gpu.vao);
        if (g_perf_depth_gpu.fbo) g_depth_gpu_api.delete_fbos(1, &g_perf_depth_gpu.fbo);
        if (g_perf_depth_gpu.sampler) g_depth_gpu_api.delete_samplers(1, &g_perf_depth_gpu.sampler);
        memset(&g_perf_depth_gpu, 0, sizeof g_perf_depth_gpu);
        g_perf_depth_gpu.context = context;
        g_perf_depth_gpu.failed = 1;
    }
    return ok;
}

static int perf_depth_gpu_copy(GLuint source, GLuint destination,
                              GLint sx, GLint sy, GLint dx, GLint dy,
                              GLsizei width, GLsizei height)
{
    if (!g_perf_depth_copy_gpu_on || !source || !destination || source == destination ||
        sx < 0 || sy < 0 || dx < 0 || dy < 0 || width <= 0 || height <= 0 ||
        !perf_depth_gpu_resolve()) return 0;
    HGLRC context = g_depth_gpu_api.context();
    if (!context) return 0;
    GLboolean feedback = 0;
    g_depth_gpu_api.get_b(0x8E24 /* TRANSFORM_FEEDBACK_ACTIVE */, &feedback);
    if (feedback) return 0;
    GLint read_fbo = 0, read_texture = 0, read_type = 0, read_level = -1;
    g_depth_gpu_api.get_i(0x8CAA, &read_fbo);
    if (!read_fbo) return 0;
    g_depth_gpu_api.fbo_attachment((GLuint)read_fbo,0x8D00,0x8CD0,&read_type);
    g_depth_gpu_api.fbo_attachment((GLuint)read_fbo,0x8D00,0x8CD1,&read_texture);
    if (read_type != 0x1702 || (GLuint)read_texture != source) return 0;
    g_depth_gpu_api.fbo_attachment((GLuint)read_fbo,0x8D00,0x8CD2,&read_level);
    if (read_level != 0) return 0;
    const GLenum query_targets[] = {0x8914,0x8C2F,0x8D6A,0x8C87};
    for (unsigned i = 0; i < sizeof query_targets / sizeof query_targets[0]; i++) {
        GLint active = 0;
        g_depth_gpu_api.get_query(query_targets[i], 0x8865, &active);
        if (active) return 0;
    }
    GLint source_target = 0, dest_target = 0, source_base = 0, source_mode = 0;
    GLint sw = 0, sh = 0, dw = 0, dh = 0;
    g_depth_gpu_api.get_tex(source, 0x1006, &source_target);
    g_depth_gpu_api.get_tex(destination, 0x1006, &dest_target);
    g_depth_gpu_api.get_tex(source, 0x813C, &source_base);
    g_depth_gpu_api.get_tex(source, 0x90EA, &source_mode);
    g_depth_gpu_api.get_level(source, 0, 0x1000, &sw);
    g_depth_gpu_api.get_level(source, 0, 0x1001, &sh);
    g_depth_gpu_api.get_level(destination, 0, 0x1000, &dw);
    g_depth_gpu_api.get_level(destination, 0, 0x1001, &dh);
    if (g_depth_gpu_api.error() || source_target != 0x0DE1 || dest_target != 0x0DE1 ||
        source_base != 0 || source_mode != 0x1902 ||
        width > sw || height > sh || sx > sw - width || sy > sh - height ||
        width > dw || height > dh || dx > dw - width || dy > dh - height ||
        !perf_depth_gpu_prepare(context)) return 0;

    /* Only viewport/scissor zero are used; leave other indexed state intact. */
    const GLenum caps[] = {0x0B71,0x0B90,0x0B44,0x8C89,0x8037,0x809E,0x80A0,0x8E51,
                           0x3000,0x3001,0x3002,0x3003,0x3004,0x3005,0x3006,0x3007};
    GLboolean enabled[sizeof caps / sizeof caps[0]], scissor, mask;
    GLint program, vao, fbo, active_texture, texture, sampler, func, polygon[2], origin, clip_depth;
    float viewport[4];
    double range[2];
    g_depth_gpu_api.get_i(0x8B8D, &program);
    g_depth_gpu_api.get_i(0x85B5, &vao);
    g_depth_gpu_api.get_i(0x8CA6, &fbo);
    g_depth_gpu_api.get_i(0x84E0, &active_texture);
    g_depth_gpu_api.active_texture(0x84C0);
    g_depth_gpu_api.get_i(0x8069, &texture);
    g_depth_gpu_api.get_i(0x8919, &sampler);
    g_depth_gpu_api.get_i(0x0B74, &func);
    g_depth_gpu_api.get_b(0x0B72, &mask);
    g_depth_gpu_api.get_i(0x0B40, polygon);
    g_depth_gpu_api.get_i(0x935C, &origin);
    g_depth_gpu_api.get_i(0x935D, &clip_depth);
    g_depth_gpu_api.get_fi(0x0BA2, 0, viewport);
    g_depth_gpu_api.get_di(0x0B70, 0, range);
    scissor = g_depth_gpu_api.enabled_i(0x0C11, 0);
    for (unsigned i = 0; i < sizeof caps / sizeof caps[0]; i++) {
        enabled[i] = g_depth_gpu_api.enabled(caps[i]);
        if (i == 0) g_depth_gpu_api.enable(caps[i]);
        else g_depth_gpu_api.disable(caps[i]);
    }
    g_depth_gpu_api.disable_i(0x0C11, 0);
    g_depth_gpu_api.depth_func(0x0207 /* ALWAYS */);
    g_depth_gpu_api.depth_mask(1);
    g_depth_gpu_api.polygon_mode(0x0408, 0x1B02);
    g_depth_gpu_api.clip_control(0x8CA1, 0x935E);
    g_depth_gpu_api.depth_range(0, 0, 1);
    g_depth_gpu_api.viewport(0, (float)dx, (float)dy, (float)width, (float)height);
    g_depth_gpu_api.fbo_texture(g_perf_depth_gpu.fbo, 0x8D00, destination, 0);
    int ok = g_depth_gpu_api.fbo_status(g_perf_depth_gpu.fbo, 0x8D40) == 0x8CD5;
    if (ok) {
        g_depth_gpu_api.bind_fbo(0x8CA9, g_perf_depth_gpu.fbo);
        g_depth_gpu_api.bind_texture(0x0DE1, source);
        g_depth_gpu_api.bind_sampler(0, g_perf_depth_gpu.sampler);
        g_depth_gpu_api.bind_vao(g_perf_depth_gpu.vao);
        g_depth_gpu_api.use(g_perf_depth_gpu.program);
        g_depth_gpu_api.uniform2i(g_perf_depth_gpu.program, 0, sx - dx, sy - dy);
        g_depth_gpu_api.draw(0x0004, 0, 3);
        ok = g_depth_gpu_api.error() == 0;
    }
    g_depth_gpu_api.use((GLuint)program);
    g_depth_gpu_api.bind_vao((GLuint)vao);
    g_depth_gpu_api.bind_sampler(0, (GLuint)sampler);
    g_depth_gpu_api.bind_texture(0x0DE1, (GLuint)texture);
    g_depth_gpu_api.active_texture((GLenum)active_texture);
    g_depth_gpu_api.bind_fbo(0x8CA9, (GLuint)fbo);
    g_depth_gpu_api.fbo_texture(g_perf_depth_gpu.fbo, 0x8D00, 0, 0);
    g_depth_gpu_api.viewport(0, viewport[0], viewport[1], viewport[2], viewport[3]);
    g_depth_gpu_api.depth_range(0, range[0], range[1]);
    g_depth_gpu_api.clip_control((GLenum)origin, (GLenum)clip_depth);
    if (polygon[0] == polygon[1]) g_depth_gpu_api.polygon_mode(0x0408, (GLenum)polygon[0]);
    else {
        g_depth_gpu_api.polygon_mode(0x0404, (GLenum)polygon[0]);
        g_depth_gpu_api.polygon_mode(0x0405, (GLenum)polygon[1]);
    }
    g_depth_gpu_api.depth_func((GLenum)func);
    g_depth_gpu_api.depth_mask(mask);
    if (scissor) g_depth_gpu_api.enable_i(0x0C11, 0);
    for (unsigned i = 0; i < sizeof caps / sizeof caps[0]; i++) {
        if (enabled[i]) g_depth_gpu_api.enable(caps[i]);
        else g_depth_gpu_api.disable(caps[i]);
    }
    return ok && g_depth_gpu_api.error() == 0;
}
