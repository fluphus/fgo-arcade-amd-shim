/* Parallel depth reduction for the two verified tile-light vertex programs.
   Unknown sources keep the original path. No cross-frame depth caching. */
typedef struct {
    int near_depth;
    float constants[44];
    GLint sampler_location[2], sampler_unit[2];
    GLuint block_index[3];
} perf_tile_program;
static perf_tile_program *g_perf_tile_programs[65536];
static GLuint g_perf_tile_vs[2], g_perf_tile_cs[2], g_perf_tile_buffer;
static GLint g_perf_tile_cs_sampler[2][2];
static HGLRC g_perf_tile_context;
static int g_perf_tile_disabled, g_perf_tile_active;
static GLuint g_perf_tile_saved_program, g_perf_tile_saved_buffer;
static GLint g_perf_tile_saved_generic;
static GLint64 g_perf_tile_saved_offset, g_perf_tile_saved_size;
static unsigned long long g_perf_tile_draws __attribute__((used));
static unsigned long long g_perf_tile_fallbacks __attribute__((used));
static int g_perf_tile_on=1;

static struct {
    void (WINAPI *gen)(GLsizei,GLuint *);
    void (WINAPI *data)(GLuint,GLsizeiptr,const void *,GLenum);
    void (WINAPI *base)(GLenum,GLuint,GLuint);
    void (WINAPI *range)(GLenum,GLuint,GLuint,GLintptr,GLsizeiptr);
    void (WINAPI *bind)(GLenum,GLuint);
    void (WINAPI *dispatch)(GLuint,GLuint,GLuint);
    void (WINAPI *barrier)(GLbitfield);
    void (WINAPI *u1f)(GLuint,GLint,float);
    GLuint (WINAPI *resource)(GLuint,GLenum,const char *);
    GLboolean (WINAPI *enabled)(GLenum);
} g_perf_tile_gl;

static int perf_tile_api(void)
{
    if (g_perf_tile_gl.dispatch) return 1;
    if (!perf_rs_api()) return 0;
#define TILE_API(field,name) do { *(PROC *)&g_perf_tile_gl.field=perf_rs_proc(name); \
    if (!g_perf_tile_gl.field) return 0; } while (0)
    TILE_API(gen,"glCreateBuffers"); TILE_API(data,"glNamedBufferData");
    TILE_API(base,"glBindBufferBase"); TILE_API(range,"glBindBufferRange");
    TILE_API(bind,"glBindBuffer"); TILE_API(barrier,"glMemoryBarrier");
    TILE_API(u1f,"glProgramUniform1f"); TILE_API(resource,"glGetProgramResourceIndex");
    TILE_API(enabled,"glIsEnabled"); TILE_API(dispatch,"glDispatchCompute");
#undef TILE_API
    return 1;
}

static int perf_tile_source_kind(const char *src, int length)
{
    if (!src || length<9000 || length>12000) return -1;
    uint64_t hash=14695981039346656037ULL;
    for (int i=0;i<length;i++) if (src[i]!='\r') {
        hash^=(unsigned char)src[i]; hash*=1099511628211ULL;
    }
    if (hash==0x2132936cb5ef9411ULL) return 1;
    if (hash==0x76950ee37edfb114ULL) return 0;
    return -1;
}

static void perf_tile_forget(GLuint program)
{
    if (program>=65536) return;
    free(g_perf_tile_programs[program]); g_perf_tile_programs[program]=NULL;
}

static void perf_tile_link(GLuint program)
{
    perf_tile_forget(program);
    if (!g_perf_tile_on || program>=65536 || g_prog_shader_count[program]!=1) return;
    GLuint shader=g_prog_shaders[program][0];
    if (shader>=65536 || g_shader_types[shader]!=0x8B31) return;
    int kind=perf_tile_source_kind(g_shader_src[shader].src,g_shader_src[shader].len);
    if (kind<0 || !perf_tile_api()) return;
    GLint linked=0,feedback=0;
    g_perf_rs_gl.program_iv(program,0x8B82,&linked);
    g_perf_rs_gl.program_iv(program,0x8C83,&feedback);
    if (!linked || feedback) return;
    perf_tile_program *p=calloc(1,sizeof *p);
    if (!p) return;
    p->near_depth=kind;
    p->sampler_location[0]=g_perf_rs_gl.location(program,"g_opaque_only_raw_depth_sampler");
    p->sampler_location[1]=g_perf_rs_gl.location(program,"g_full_raw_depth_sampler");
    p->sampler_unit[1]=1;
    const char *blocks[]={"TileSet","LightIndices","LightData"};
    for (int i=0;i<3;i++) p->block_index[i]=g_perf_tile_gl.resource(program,0x92E6,blocks[i]);
    g_perf_tile_programs[program]=p;
}

static void perf_tile_uniform4(GLuint program, GLint location, GLsizei count, const float *values)
{
    if (program>=65536 || !g_perf_tile_programs[program] || !values || location<0 ||
        location>=11 || count<=0) return;
    if (count>11-location) count=11-location;
    memcpy(g_perf_tile_programs[program]->constants+4*location,values,(size_t)count*16);
}

static void perf_tile_sampler(GLuint program, GLint location, GLuint64 unit, int handle)
{
    if (program>=65536 || !g_perf_tile_programs[program] || location<0) return;
    perf_tile_program *p=g_perf_tile_programs[program];
    for (int i=0;i<2;i++) if (location==p->sampler_location[i]) {
        if (handle) { perf_tile_forget(program); return; }
        p->sampler_unit[i]=(GLint)unit;
    }
}

static void perf_tile_storage_binding(GLuint program, GLuint index, GLuint binding)
{
    if (program>=65536 || !g_perf_tile_programs[program]) return;
    const GLuint wanted[]={0,1,36};
    for (int i=0;i<3;i++) if (g_perf_tile_programs[program]->block_index[i]==index && binding!=wanted[i]) {
        perf_tile_forget(program); return;
    }
}

static GLuint perf_tile_compile(GLenum type, const char *source)
{
    GLint ok=0;
    GLuint shader=g_perf_rs_gl.create_shader(type), program=0;
    if (!shader) return 0;
    g_perf_rs_gl.source(shader,1,&source,NULL);
    g_perf_rs_gl.compile(shader);
    g_perf_rs_gl.shader_iv(shader,0x8B81,&ok);
    if (ok) {
        program=g_perf_rs_gl.create_program();
        g_perf_rs_gl.attach(program,shader); g_perf_rs_gl.link(program);
        g_perf_rs_gl.program_iv(program,0x8B82,&ok);
        if (!ok) { g_perf_rs_gl.delete_program(program); program=0; }
    }
    g_perf_rs_gl.delete_shader(shader);
    return program;
}

static const char g_perf_tile_compute[]=
    "#version 430\n#define USE_NEAR %d\n"
    "layout(local_size_x=32) in;\n"
    "layout(binding=0) uniform sampler2D opaque_depth;\n"
    "layout(binding=1) uniform sampler2D full_depth;\n"
    "layout(location=0) uniform float resolution_scale;\n"
    "layout(std430,binding=47) writeonly buffer TileDepth {vec2 tile_depth[];};\n"
    "shared float row_near[32],row_far[32];\n"
    "void main(){uint row=gl_LocalInvocationID.x;"
    "vec2 offset=vec2(1.f/1920.f,1.f/1080.f);"
    "vec2 uv=vec2(float(gl_WorkGroupID.x*60u)*offset.x,float(gl_WorkGroupID.y*60u)*offset.y);"
    "uv*=resolution_scale;offset*=resolution_scale;offset*=2.f;"
    "float max_uv=resolution_scale-1.f/1080.f;vec2 t=uv;"
    "for(uint i=0u;i<row;i++)t.y=min(t.y+offset.y,max_uv);"
    "float dn=0.f,df=10000.f;if(row<31u){\n"
    "#if USE_NEAR\n"
    "for(int j=0;j<=30;j++){dn=max(dn,textureLod(full_depth,t,0.f).r);t.x=min(t.x+offset.x,max_uv);}\n"
    "#endif\n"
    "t.x=uv.x;for(int j=0;j<=30;j++){df=min(df,textureLod(opaque_depth,t,0.f).r);t.x=min(t.x+offset.x,max_uv);}}"
    "row_near[row]=dn;row_far[row]=df;barrier();"
    "for(uint step=16u;step>0u;step>>=1u){if(row<step){"
    "row_near[row]=max(row_near[row],row_near[row+step]);"
    "row_far[row]=min(row_far[row],row_far[row+step]);}barrier();}"
    "if(row==0u)tile_depth[gl_WorkGroupID.y*32u+gl_WorkGroupID.x]=vec2(row_far[0],row_near[0]);}\n";

static int perf_tile_build(GLuint original, int kind)
{
    if (g_perf_tile_vs[kind] && g_perf_tile_cs[kind]) return 1;
    GLint limit=0;
    g_perf_rs_gl.get(0x90DD,&limit);
    if (limit<=47) return 0;
    GLuint shader=g_prog_shaders[original][0];
    const char *src=g_shader_src[shader].src;
    const char *main=strstr(src,"void main(){");
    const char *begin=strstr(src,"\tfloat df = 10000.f;");
    const char *end=begin?strstr(begin,"#endif//USE_MINMAX_FILTER"):NULL;
    if (!main || !begin || !end || main>=begin) return 0;
    end+=strlen("#endif//USE_MINMAX_FILTER");
    char *variant=malloc(strlen(src)+256);
    if (!variant) return 0;
    char *at=variant;
    memcpy(at,src,(size_t)(main-src)); at+=main-src;
    const char *decl="layout(std430,binding=47) readonly buffer TileDepth {vec2 tile_depth[];};\n";
    memcpy(at,decl,strlen(decl)); at+=strlen(decl);
    memcpy(at,main,(size_t)(begin-main)); at+=begin-main;
    const char *replacement="vec2 bounds=tile_depth[tile_y_idx*32u+tile_x_idx];\nfloat df=bounds.x;\nfloat dn=bounds.y;\n";
    memcpy(at,replacement,strlen(replacement)); at+=strlen(replacement);
    strcpy(at,end);
    GLuint vs=perf_tile_compile(0x8B31,variant); free(variant);
    char compute[sizeof g_perf_tile_compute+32];
    snprintf(compute,sizeof compute,g_perf_tile_compute,kind);
    GLuint cs=perf_tile_compile(0x91B9,compute);
    if (!vs || !cs) {
        if (vs) g_perf_rs_gl.delete_program(vs);
        if (cs) g_perf_rs_gl.delete_program(cs);
        return 0;
    }
    if (!g_perf_tile_buffer) {
        g_perf_tile_gl.gen(1,&g_perf_tile_buffer);
        g_perf_tile_gl.data(g_perf_tile_buffer,32*18*8,NULL,0x88E8);
    }
    g_perf_tile_vs[kind]=vs; g_perf_tile_cs[kind]=cs;
    g_perf_tile_cs_sampler[kind][0]=g_perf_rs_gl.location(cs,"opaque_depth");
    g_perf_tile_cs_sampler[kind][1]=g_perf_rs_gl.location(cs,"full_depth");
    return g_perf_tile_buffer!=0;
}

static int perf_tile_begin(GLenum mode, GLint first, GLsizei count, GLsizei instances)
{
    if (!g_perf_tile_on || g_perf_tile_disabled || g_perf_tile_active || mode!=0 ||
        first!=0 || count!=1152 || instances!=1 || g_current_program>=65536 ||
        !g_perf_tile_programs[g_current_program]) return 0;
    /* Avoid introducing private object names during early initialization. */
    if (!g_perf_rs_draws) return 0;
    perf_rs_close_pending();
    HGLRC context=g_perf_rs_gl.context();
    if (!context) return 0;
    if (g_perf_tile_context && g_perf_tile_context!=context) {
        g_perf_tile_disabled=1; return 0;
    }
    g_perf_tile_context=context;
    GLint current=0,feedback=0;
    g_perf_rs_gl.get(0x8B8D,&current); g_perf_rs_gl.get(0x8E24,&feedback);
    if ((GLuint)current!=g_current_program || feedback || !g_perf_tile_gl.enabled(0x8C89)) return 0;
    perf_tile_program *p=g_perf_tile_programs[g_current_program];
    int kind=p->near_depth;
    if (!perf_tile_build(g_current_program,kind)) {
        g_perf_tile_disabled=1; g_perf_tile_fallbacks++; return 0;
    }
    GLint buffer=0;
    g_perf_rs_gl.get_i(0x90D3,47,&buffer);
    g_perf_rs_gl.get_i64(0x90D4,47,&g_perf_tile_saved_offset);
    g_perf_rs_gl.get_i64(0x90D5,47,&g_perf_tile_saved_size);
    g_perf_rs_gl.get(0x90D3,&g_perf_tile_saved_generic);
    g_perf_tile_saved_buffer=(GLuint)buffer;
    g_perf_tile_saved_program=g_current_program;
    GLuint cs=g_perf_tile_cs[kind], vs=g_perf_tile_vs[kind];
    g_perf_tile_gl.base(0x90D2,47,g_perf_tile_buffer);
    g_perf_rs_gl.u1(cs,g_perf_tile_cs_sampler[kind][0],p->sampler_unit[0]);
    if (kind) g_perf_rs_gl.u1(cs,g_perf_tile_cs_sampler[kind][1],p->sampler_unit[1]);
    g_perf_tile_gl.u1f(cs,0,p->constants[23]);
    g_perf_rs_gl.use(cs); g_perf_tile_gl.dispatch(32,18,1);
    g_perf_tile_gl.barrier(0x2000);
    g_perf_rs_gl.u4(vs,0,11,p->constants);
    g_perf_rs_gl.use(vs);
    g_perf_tile_active=1; g_perf_tile_draws++;
    return 1;
}

static void perf_tile_end(void)
{
    if (!g_perf_tile_active) return;
    g_perf_tile_active=0;
    g_perf_rs_gl.use(g_perf_tile_saved_program);
    if (g_perf_tile_saved_buffer && g_perf_tile_saved_size>0)
        g_perf_tile_gl.range(0x90D2,47,g_perf_tile_saved_buffer,
                            (GLintptr)g_perf_tile_saved_offset,(GLsizeiptr)g_perf_tile_saved_size);
    else g_perf_tile_gl.base(0x90D2,47,g_perf_tile_saved_buffer);
    g_perf_tile_gl.bind(0x90D2,(GLuint)g_perf_tile_saved_generic);
}
