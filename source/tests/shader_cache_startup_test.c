#include "../shim.c"

static int submits, resolves, unavailable;
static unsigned long long submitted_hash;
static void WINAPI startup_source(GLuint shader, GLsizei count,
                                  const char *const *sources, const GLint *lengths)
{
    (void)shader;
    if (count != 1 || !sources || !sources[0]) abort();
    int len = lengths && lengths[0] >= 0 ? lengths[0] : (int)strlen(sources[0]);
    submitted_hash = dataflow_hash_bytes((const unsigned char *)sources[0], (size_t)len);
    submits++;
}

static PROC WINAPI startup_resolve(LPCSTR name)
{
    if (strcmp(name, "glShaderSource")) return NULL;
    resolves++;
    return unavailable ? NULL : (PROC)startup_source;
}

int main(int argc, char **argv)
{
    const GLuint shader = argc == 2 && !strcmp(argv[1], "other-id") ? 64999 : 65000;
    const char *source = "#version 450 core\nvoid main(){gl_Position=vec4(0.0);}";
    GLint len = (GLint)strlen(source);
    char path[MAX_PATH * 4];
    unsigned long long ha, hb;
    int seed = argc == 2 && !strcmp(argv[1], "seed");
    unavailable = argc == 2 && !strcmp(argv[1], "unavailable");
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    g_self_module = GetModuleHandleA(NULL);
    g_shader_cache_on = 1;
    g_telemetry_quiet = 1;
    g_shader_types[shader] = 0x8B31;
    real_wglGetProcAddress = startup_resolve;
    /* Deliberately do not pre-seed real_glShaderSource: every invocation is
       a fresh process, exactly like the game's first source after restart. */
    if (!shader_cache_path(shader, 0x8B31, source, len, path, sizeof path, &ha, &hb)) return 2;
    if (seed) DeleteFileA(path);
    else if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) return 3;
    glShaderSource_shim(shader, 1, &source, &len);
    printf("mode=%s submits=%d resolves=%d hash=%016llx\n",
           argc == 2 ? argv[1] : "warm", submits, resolves, submitted_hash);
    if (unavailable) return submits == 0 && resolves == 1 ? 0 : 4;
    return submits == 1 && resolves == 1 &&
           GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES ? 0 : 5;
}
