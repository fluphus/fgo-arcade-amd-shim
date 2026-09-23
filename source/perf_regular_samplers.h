/* Private ordinary-sampler variant; application programs remain authoritative. */
#define PERF_RS_SLOTS 32
#define PERF_RS_VALUES 64
static unsigned long long g_perf_rs_draws, g_perf_rs_fallbacks;
static unsigned long long g_perf_rs_ui_draws, g_perf_rs_ui_fallbacks;
static unsigned long long g_perf_rs_handle_fallbacks;
/* Query reductions are exported as counters so a live scene can prove that
   the cache is active.  They count skipped driver queries, not guessed work. */
static unsigned long long g_perf_rs_rejects[8];
static GLuint g_perf_rs_missing_buffer;

static char *perf_rs_replace(char *src, const char *from, const char *to)
{
    size_t a = strlen(from), b = strlen(to), n = 0;
    const char *p = src;
    while ((p = strstr(p, from))) { n++; p += a; }
    if (!n) return src;
    size_t size = strlen(src) + 1;
    if (b >= a) size += n * (b - a); else size -= n * (a - b);
    char *out = malloc(size), *dst = out;
    if (!out) { free(src); return NULL; }
    p = src;
    const char *at;
    while ((at = strstr(p, from))) {
        size_t len = (size_t)(at - p);
        memcpy(dst, p, len); dst += len;
        memcpy(dst, to, b); dst += b; p = at + a;
    }
    strcpy(dst, p); free(src); return out;
}

static int perf_rs_ui_texture_count(const char *source)
{
    if (!source) return 0;
    const char *count=strstr(source,"#define TextureCount ");
    if (!count) return 0;
    count+=strlen("#define TextureCount ");
    if ((*count!='1' && *count!='2') || (count[1]!='\r' && count[1]!='\n')) return 0;
    static const char *empty[]={"#define CALCULATE_MASK_TEXCOORD()",
        "#define MASK_TEXCOORD", "#define MULTIPLY_MASK_TEXTURE()"};
    for (unsigned i=0;i<3;i++) {
        const char *at=strstr(source,empty[i]);
        if (!at) return 0;
        at+=strlen(empty[i]);
        if (*at!='\r' && *at!='\n') return 0;
    }
    if (strstr(source,"out VertexData{") &&
        strstr(source,"layout(binding = 0) uniform Common{") &&
        strstr(source,"vec4 g_transform[4]") &&
        strstr(source,"result.color = a_color / 255.f;")) return *count-'0';
    if (strstr(source,"in VertexData{") &&
        strstr(source,"get_texture_handle(index) packUint2x32(floatBitsToUint(g_constants[index].xy))") &&
        strstr(source,"get_texture_combiner(index) floatBitsToUint(g_constants[index].z)") &&
        strstr(source,"sampler2D(unpackUint2x32(get_texture_handle(i)))") &&
        strstr(source,"layout(location = 0) out vec4 result;")) return *count-'0';
    return 0;
}

static int perf_rs_alpha_depth_source(const char *source)
{
    if (!source || !strstr(source,"_amdshim_map_handle_pairs")) return 0;
    if (!strstr(source,"#define HAVE_VERTEX_COLOR 0\n") &&
        !strstr(source,"#define HAVE_VERTEX_COLOR 0\r\n") &&
        !((strstr(source,"#define HAVE_VERTEX_COLOR 1\n") ||
           strstr(source,"#define HAVE_VERTEX_COLOR 1\r\n")) &&
          (strstr(source,"#define SHADER_LOD 0\n") ||
           strstr(source,"#define SHADER_LOD 0\r\n")))) return 0;
    static const char *macros[]={"#define DEPTH_ONLY 1", "#define ENABLE_PUNCH 1",
        "#define LIGHTING_TYPE 0", "#define EFFECT_SHADER 0",
        "#define TESSELLATION_SHADER 0", "#define ENABLE_DITHER 0",
        "#define SAMPLER_INDEX_OPACITY0 16"};
    for (unsigned i=0;i<sizeof macros/sizeof macros[0];i++) {
        const char *at=strstr(source,macros[i]);
        if (!at) return 0;
        at+=strlen(macros[i]);
        if (*at!='\r' && *at!='\n') return 0;
    }
    return 1;
}

static int perf_rs_npr_source(const char *source)
{
    if (!source || !strstr(source,"_amdshim_map_handle_pairs") ||
        !strstr(source,"g_unique_shadow_map_handle") ||
        strstr(source,"#define EFFECT_SHADER")) return 0;
    static const char *macros[]={"#define DEPTH_ONLY 0", "#define LIGHTING_TYPE 1",
        "#define HAVE_VERTEX_COLOR 0", "#define SHADER_SUB_TYPE0 0", "#define SHADER_LOD 1",
        "#define TESSELLATION_SHADER 0", "#define ENABLE_DITHER 0",
        "#define ExtraShaderFlag_UseUniqueShadow 4"};
    for (unsigned i=0;i<sizeof macros/sizeof macros[0];i++) {
        const char *at=strstr(source,macros[i]);
        if (!at) return 0;
        at+=strlen(macros[i]);
        if (*at!='\r' && *at!='\n') return 0;
    }
    const char *punch=strstr(source,"#define ENABLE_PUNCH ");
    if (!punch) return 0;
    punch+=strlen("#define ENABLE_PUNCH ");
    return (*punch=='0' || *punch=='1') && (punch[1]=='\r' || punch[1]=='\n');
}

static int perf_rs_grass_source(const char *source)
{
    if (!source) return 0;
    static const char *macros[]={"#define DEPTH_ONLY 0", "#define SHADOW_MODE 1",
        "#define USE_DEFAULT_VS_CONSTANTS 0"};
    for (unsigned i=0;i<sizeof macros/sizeof macros[0];i++) {
        const char *at=strstr(source,macros[i]);
        if (!at) return 0;
        at+=strlen(macros[i]);
        if (*at!='\r' && *at!='\n') return 0;
    }
    if (strstr(source,"#define VERTEX_SHADER 1\n") ||
        strstr(source,"#define VERTEX_SHADER 1\r\n"))
        return strstr(source,"layout(location = 0) uniform vec4 g_constants[11];") &&
            strstr(source,"#define VertexCountPerBlade ((Division + 1) * 2)") &&
            strstr(source,"buffer PtrBuf_2 { Blade g_blades[]; };") &&
            strstr(source,"Blade b = g_blades[bidx + g_blade_offset];");
    if (!strstr(source,"#define VERTEX_SHADER 0\n") &&
        !strstr(source,"#define VERTEX_SHADER 0\r\n")) return 0;
    return strstr(source,"layout(location = 11) uniform vec4 g_fconstants[2];") &&
        strstr(source,"#define g_color g_fconstants[0].rgb") &&
        strstr(source,"layout(binding = 3) uniform sampler2D g_color_sampler;") &&
        strstr(source,"const float shadow = min(calculate_ssao(0.5f), calculate_shadow());") &&
        strstr(source,"sampler2D(unpackUint2x32(g_screen_shadow_map_handle))") &&
        strstr(source,"sampler2D(unpackUint2x32(g_ssao_map_handle))") &&
        strstr(source,"sampler2DArrayShadow(unpackUint2x32(g_local_shadow_map_handle))");
}

static int perf_rs_foliage_source(const char *source)
{
    if (!source || !strstr(source,"_amdshim_map_handle_pairs") ||
        !strstr(source,"layout(location = 10) uniform vec4 g_perturbations[2];") ||
        !strstr(source,"#define USE_PREFILTERED_SHADOW 1//") ||
        strstr(source,"#define EFFECT_SHADER") || strstr(source,"#define UboIndex_PerGrassDraw")) return 0;
    static const char *macros[]={"#define HAVE_TANGENT 0", "#define SHADOW_MODE 0",
        "#define USE_CLIP_PLANE 0"};
    for (unsigned i=0;i<sizeof macros/sizeof macros[0];i++) {
        const char *at=strstr(source,macros[i]);
        if (!at) return 0;
        at+=strlen(macros[i]);
        if (*at!='\r' && *at!='\n') return 0;
    }
    int depth=0;
    for (int i=0;i<2;i++) {
        const char *at=strstr(source,i?"#define ENABLE_PUNCH ":"#define DEPTH_ONLY ");
        if (!at) return 0;
        at+=strlen(i?"#define ENABLE_PUNCH ":"#define DEPTH_ONLY ");
        if ((*at!='0' && *at!='1') || (at[1]!='\r' && at[1]!='\n')) return 0;
        if (!i) depth=*at-'0'; else if (*at-'0'!=depth) return 0;
    }
    if (strstr(source,"#define VERTEX_SHADER 1\n") ||
        strstr(source,"#define VERTEX_SHADER 1\r\n"))
        return strstr(source,"layout(binding = 28) uniform sampler2D g_perturbation_sampler;") &&
            strstr(source,"vec2 perturbate = (textureLod(g_perturbation_sampler,") &&
            strstr(source,"result.color.rgb = g_color.rgb;") ? depth+1 : 0;
    if ((!strstr(source,"#define VERTEX_SHADER 0\n") &&
         !strstr(source,"#define VERTEX_SHADER 0\r\n")) ||
        !strstr(source,"void discard_by_opacity_and_dither(in  float opacity)") ||
        !strstr(source,"blend_layered_diffuse(diffuse_color.rgb, frg.texcoords0.zw);") ||
        !strstr(source,"sampler2D(unpackUint2x32(_amdshim_map_handle(int(SAMPLER_INDEX_OPACITY0))))")) return 0;
    return depth+1;
}

static int perf_rs_macro(const char *source, const char *name, int value)
{
    char text[96];
    snprintf(text,sizeof text,"#define %s %d",name,value);
    const char *at=source?strstr(source,text):NULL;
    return at && (at[strlen(text)]=='\r' || at[strlen(text)]=='\n');
}

static int perf_rs_indirect_shadow_source(const char *source)
{
    if (!source || !strstr(source,"flat uint64_t sampler;")) return 0;
    if (strstr(source,"in VertexData{") &&
        strstr(source,"texture(sampler2D(unpackUint2x32(frg.sampler)), frg.texcoord.xy).r") &&
        strstr(source,"if(opacity < frg.texcoord.z)") &&
        strstr(source,"result = vec4(1.f);")) return 1;
    return perf_rs_macro(source,"PERSPECTIVE_TRANSFORM",0) &&
        strstr(source,"uniform int _amdshim_bindless_draw_id;") &&
        strstr(source,"}g_draws[512];") &&
        strstr(source,"vec4 m_texcoord_transforms[2];") &&
        strstr(source,"vec4 m_dissolve_protect_sphere;") &&
        strstr(source,"vec2 m_dissolve_noise_texcoord_scale;") &&
        strstr(source,"result.sampler = g_draws[_amdshim_bindless_draw_id].m_opacity_map;");
}

static int perf_rs_regular_lod0_source(const char *source)
{
    if (!source || !strstr(source,"_amdshim_map_handle_pairs") ||
        !strstr(source,"g_reflection_proxy_count")) return 0;
    return perf_rs_macro(source,"SHADER_LOD",0) &&
        perf_rs_macro(source,"DEPTH_ONLY",0) && perf_rs_macro(source,"EFFECT_SHADER",0) &&
        perf_rs_macro(source,"TESSELLATION_SHADER",0) && perf_rs_macro(source,"ENABLE_DITHER",0) &&
        perf_rs_macro(source,"SHADER_SUB_TYPE0",0) &&
        (perf_rs_macro(source,"HAVE_VERTEX_COLOR",0) || perf_rs_macro(source,"HAVE_VERTEX_COLOR",1)) &&
        (perf_rs_macro(source,"LIGHTING_TYPE",0) || perf_rs_macro(source,"LIGHTING_TYPE",1));
}

static int perf_rs_wind_depth_source(const char *source)
{
    if (perf_rs_macro(source,"AMD_SHIM_REGULAR_DEPTH_TRANSLATED",1) &&
        perf_rs_macro(source,"DEPTH_ONLY",1) && perf_rs_macro(source,"ENABLE_PUNCH",0) &&
        strstr(source,"void main(){ result = vec4(1.0); }")) return 1;
    return source && strstr(source,"_amdshim_map_handle_pairs") &&
        perf_rs_macro(source,"DEPTH_ONLY",1) && perf_rs_macro(source,"ENABLE_PUNCH",0) &&
        perf_rs_macro(source,"LIGHTING_TYPE",0) && perf_rs_macro(source,"EFFECT_SHADER",0) &&
        perf_rs_macro(source,"TESSELLATION_SHADER",0) && perf_rs_macro(source,"SHADER_LOD",0) &&
        perf_rs_macro(source,"HAVE_VERTEX_COLOR",1) && perf_rs_macro(source,"VERTEX_SHADER",1);
}

static char *perf_rs_wind_vertex(char *out)
{
    if (!perf_rs_macro(out,"HAVE_VERTEX_COLOR",1)) return out;
    out=perf_rs_replace(out,"sampler2D(unpackUint2x32(g_wind_perturbation_map_handle))","_perf_rs_handle_0");
    if (!out) return NULL;
    return perf_rs_replace(out,"#define SAMPLER_INDEX_NORMAL0 0",
        "uniform sampler2D _perf_rs_handle_0;\n#define SAMPLER_INDEX_NORMAL0 0");
}

static char *perf_rs_source(const char *source)
{
    if (perf_rs_indirect_shadow_source(source)) {
        char *out=malloc(strlen(source)+1);
        if (!out) return NULL;
        strcpy(out,source);
        if (strstr(source,"in VertexData{")) {
            out=perf_rs_replace(out,"sampler2D(unpackUint2x32(frg.sampler))","_perf_rs_indirect_opacity");
            if (!out) return NULL;
            return perf_rs_replace(out,"layout(location = 0) out vec4 result;",
                "uniform sampler2D _perf_rs_indirect_opacity;\nlayout(location = 0) out vec4 result;");
        }
        if (!strstr(source,"sampler2D(unpackUint2x32(g_wind_perturbation_map_handle))")) return out;
        out=perf_rs_replace(out,"sampler2D(unpackUint2x32(g_wind_perturbation_map_handle))","_perf_rs_handle_0");
        if (!out) return NULL;
        return perf_rs_replace(out,"uniform int _amdshim_bindless_draw_id;",
            "uniform sampler2D _perf_rs_handle_0;\nuniform int _amdshim_bindless_draw_id;");
    }
    if (perf_rs_wind_depth_source(source)) {
        char *out=malloc(strlen(source)+1);
        if (!out) return NULL;
        strcpy(out,source);
        return perf_rs_wind_vertex(out);
    }
    int foliage=perf_rs_foliage_source(source);
    if (foliage==1) {
        char *out=malloc(strlen(source)+1);
        if (!out) return NULL;
        strcpy(out,source);
        if (strstr(source,"#define VERTEX_SHADER 1")) return out;
        static const char *maps[]={"DIFFUSE0", "SPECULAR0", "OPACITY0", "LAYERED_DIFFUSE"};
        static const int indices[]={4,8,16,18};
        for (unsigned i=0;i<sizeof indices/sizeof indices[0];i++) {
            char from[160],to[64];
            snprintf(from,sizeof from,"sampler2D(unpackUint2x32(_amdshim_map_handle(int(SAMPLER_INDEX_%s))))",maps[i]);
            snprintf(to,sizeof to,"_perf_rs_foliage_%d",indices[i]);
            out=perf_rs_replace(out,from,to);
            if (!out) return NULL;
        }
        static const char *names[]={"g_screen_shadow_map_handle", "g_ssao_map_handle", "g_local_shadow_map_handle"};
        for (unsigned i=0;i<3;i++) {
            char from[160],to[64];
            snprintf(from,sizeof from,"%s(unpackUint2x32(%s))",i==2?"sampler2DArrayShadow":"sampler2D",names[i]);
            snprintf(to,sizeof to,"_perf_rs_handle_%u",i+3);
            out=perf_rs_replace(out,from,to);
            if (!out) return NULL;
        }
        /* The existing proxy-count check restricts this substitution to zero. */
        out=perf_rs_replace(out,"samplerCube(unpackUint2x32(g_reflection_proxies[i].m_cube_texture_handle))","g_env_sampler");
        if (!out) return NULL;
        return perf_rs_replace(out,"#define SAMPLER_INDEX_NORMAL0 0",
            "uniform sampler2D _perf_rs_foliage_4, _perf_rs_foliage_8, _perf_rs_foliage_16, _perf_rs_foliage_18;\n"
            "uniform sampler2D _perf_rs_handle_3, _perf_rs_handle_4;\n"
            "uniform sampler2DArrayShadow _perf_rs_handle_5;\n#define SAMPLER_INDEX_NORMAL0 0");
    }
    if (perf_rs_grass_source(source)) {
        char *out=malloc(strlen(source)+1);
        if (!out) return NULL;
        strcpy(out,source);
        /* Grass vertices sample noise/force/planter through ordinary units. */
        if (strstr(source,"#define VERTEX_SHADER 1")) return out;
        out=perf_rs_replace(out,"sampler2D(unpackUint2x32(g_screen_shadow_map_handle))",
            "_perf_rs_handle_3");
        if (!out) return NULL;
        out=perf_rs_replace(out,"sampler2D(unpackUint2x32(g_ssao_map_handle))",
            "_perf_rs_handle_4");
        if (!out) return NULL;
        out=perf_rs_replace(out,"sampler2DArrayShadow(unpackUint2x32(g_local_shadow_map_handle))",
            "_perf_rs_handle_5");
        if (!out) return NULL;
        return perf_rs_replace(out,"#define SAMPLER_INDEX_NORMAL0 0",
            "uniform sampler2D _perf_rs_handle_3, _perf_rs_handle_4;\n"
            "uniform sampler2DArrayShadow _perf_rs_handle_5;\n"
            "#define SAMPLER_INDEX_NORMAL0 0");
    }
    if (foliage==2 || perf_rs_alpha_depth_source(source)) {
        char *out=malloc(strlen(source)+1);
        if (!out) return NULL;
        strcpy(out,source);
        if (strstr(source,"#define VERTEX_SHADER 1")) return perf_rs_wind_vertex(out);
        const char *opacity="sampler2D(unpackUint2x32(_amdshim_map_handle(int(SAMPLER_INDEX_OPACITY0))))";
        if (!strstr(source,opacity)) { free(out); return NULL; }
        out=perf_rs_replace(out,opacity,"_perf_rs_opacity");
        if (!out) return NULL;
        const char *second="sampler2D(unpackUint2x32(_amdshim_map_handle(int(SAMPLER_INDEX_OPACITY1))))";
        if (!foliage && perf_rs_macro(source,"SHADER_LOD",0) && strstr(source,second)) {
            out=perf_rs_replace(out,second,"_perf_rs_opacity1");
            if (!out) return NULL;
            out=perf_rs_replace(out,"#define SAMPLER_INDEX_NORMAL0 0",
                "uniform sampler2D _perf_rs_opacity1;\n#define SAMPLER_INDEX_NORMAL0 0");
            if (!out) return NULL;
        }
        return perf_rs_replace(out,"#define SAMPLER_INDEX_NORMAL0 0",
            "uniform sampler2D _perf_rs_opacity;\n#define SAMPLER_INDEX_NORMAL0 0");
    }
    if (perf_rs_ui_texture_count(source)) {
        char *out=malloc(strlen(source)+1);
        if (!out) return NULL;
        strcpy(out,source);
        if (strstr(source,"out VertexData{")) return out;
        out=perf_rs_replace(out,"sampler2D(unpackUint2x32(get_texture_handle(i)))","_perf_rs_ui[i]");
        if (!out) return NULL;
        out=perf_rs_replace(out,"layout(location = 0) out vec4 result;",
            "uniform sampler2D _perf_rs_ui[TextureCount];\nlayout(location = 0) out vec4 result;");
        if (out && (strstr(out,"sampler2D(") || strstr(out,"samplerCube(") ||
                    strstr(out,"sampler2DShadow(") || strstr(out,"sampler2DArrayShadow("))) {
            free(out); return NULL;
        }
        return out;
    }
    int npr=perf_rs_npr_source(source);
    if (!npr && !perf_rs_regular_lod0_source(source) && (!source || !strstr(source, "_amdshim_map_handle_pairs") ||
        !strstr(source, "#define HAVE_VERTEX_COLOR 0") ||
        !strstr(source, "#define DEPTH_ONLY 0") ||
        !strstr(source, "#define EFFECT_SHADER 0") ||
        !strstr(source, "#define LIGHTING_TYPE 1") ||
        !strstr(source, "g_reflection_proxy_count"))) return NULL;
    char *out = malloc(strlen(source) + 1);
    if (!out) return NULL;
    strcpy(out, source);
    /* Preserve geometry arithmetic; only the vertex-color variants fetch wind. */
    if (strstr(source,"#define VERTEX_SHADER 1")) return perf_rs_wind_vertex(out);
    if (npr) {
        out=perf_rs_replace(out,
            "sampler2D(unpackUint2x32( _amdshim_map_handle(int( diffuse_sampler )) ))",
            "_perf_rs_npr(diffuse_sampler)");
        if (!out) return NULL;
        out=perf_rs_replace(out,"sampler2DShadow(unpackUint2x32(g_unique_shadow_map_handle))",
            "_perf_rs_unique_shadow");
        if (!out) return NULL;
    }
    /* The map index is uniform (material constant or a fixed normal-map call).
       Keep the integer expression and every texture instruction unchanged. */
    const char *needle = "sampler2D(unpackUint2x32(_amdshim_map_handle(";
    char *at;
    while ((at = strstr(out, needle))) {
        char *start = at + strlen(needle), *end = start;
        int depth = 1;
        for (; *end; end++) {
            if (*end == '(') depth++;
            if (*end == ')' && --depth == 0) break;
        }
        if (!*end || end[1] != ')' || end[2] != ')') { free(out); return NULL; }
        size_t before = (size_t)(at - out), expr = (size_t)(end - start);
        if (npr) {
            if (expr<6 || strncmp(start,"int(",4) || start[expr-1]!=')') { free(out); return NULL; }
            start+=4; expr-=5;
        }
        const char *prefix=npr?"_perf_rs_npr(":"_perf_rs_maps[";
        size_t prefix_len=strlen(prefix);
        size_t total = strlen(out) + 32;
        char *next = malloc(total);
        if (!next) { free(out); return NULL; }
        memcpy(next, out, before);
        memcpy(next + before, prefix, prefix_len);
        memcpy(next + before + prefix_len, start, expr);
        next[before + prefix_len + expr] = npr?')':']';
        strcpy(next + before + prefix_len + expr + 1, end + 3);
        free(out); out = next;
    }
    static const char *names[] = {
        "g_wind_perturbation_map_handle", "g_water_normal_map_handle",
        "g_planar_reflection_map_handle", "g_screen_shadow_map_handle",
        "g_ssao_map_handle", "g_local_shadow_map_handle"
    };
    for (unsigned i = 0; i < 6; i++) {
        char from[160], to[64];
        snprintf(from, sizeof from, "%s(unpackUint2x32(%s))",
                 i == 5 ? "sampler2DArrayShadow" : "sampler2D", names[i]);
        snprintf(to, sizeof to, "_perf_rs_handle_%u", i);
        out = perf_rs_replace(out, from, to);
        if (!out) return NULL;
    }
    /* Compute gradients before per-fragment cascade selection, then access
       only the selected texture. Sampler array indexing must be uniform. */
    out = perf_rs_replace(out,
        "texture(sampler2DShadow((index <= 0 ? g_cascaded_shadow_map_handle_0 : (index == 1 ? g_cascaded_shadow_map_handle_1 : g_cascaded_shadow_map_handle_2))),",
        "_perf_rs_cascade(index,");
    if (!out) return NULL;
    /* This variant is used only with zero reflection proxies. */
    out = perf_rs_replace(out,
        "samplerCube(unpackUint2x32(g_reflection_proxies[i].m_cube_texture_handle))",
        "g_env_sampler");
    if (!out) return NULL;
    const char *anchor = "#define SAMPLER_INDEX_NORMAL0 0";
    const char *decl =
        "uniform sampler2D _perf_rs_maps[19];\n"
        "uniform sampler2D _perf_rs_handle_0, _perf_rs_handle_1, _perf_rs_handle_2;\n"
        "uniform sampler2D _perf_rs_handle_3, _perf_rs_handle_4;\n"
        "uniform sampler2DArrayShadow _perf_rs_handle_5;\n"
        "uniform sampler2DShadow _perf_rs_shadow0, _perf_rs_shadow1, _perf_rs_shadow2;\n"
        "float _perf_rs_cascade(int i, vec3 uv){\n"
        "vec2 dx=dFdx(uv.xy),dy=dFdy(uv.xy);\n"
        "if(i<=0)return textureGrad(_perf_rs_shadow0,uv,dx,dy);\n"
        "if(i==1)return textureGrad(_perf_rs_shadow1,uv,dx,dy);\n"
        "return textureGrad(_perf_rs_shadow2,uv,dx,dy);}\n"
        "#define SAMPLER_INDEX_NORMAL0 0";
    out = perf_rs_replace(out, anchor, decl);
    if (!out) return NULL;
    if (npr) {
        out=perf_rs_replace(out,"uniform sampler2D _perf_rs_maps[19];",
            "#define _perf_rs_npr_idx(i) _perf_rs_npr_##i\n"
            "#define _perf_rs_npr(i) _perf_rs_npr_idx(i)\n"
            "uniform sampler2D _perf_rs_npr_0,_perf_rs_npr_1,_perf_rs_npr_2,_perf_rs_npr_3;\n"
            "uniform sampler2D _perf_rs_npr_4,_perf_rs_npr_5,_perf_rs_npr_6,_perf_rs_npr_7;\n"
            "uniform sampler2D _perf_rs_npr_8,_perf_rs_npr_9,_perf_rs_npr_10,_perf_rs_npr_11;\n"
            "uniform sampler2D _perf_rs_npr_12,_perf_rs_npr_13,_perf_rs_npr_14,_perf_rs_npr_15;\n"
            "uniform sampler2D _perf_rs_npr_16,_perf_rs_npr_17,_perf_rs_npr_18;");
        if (!out) return NULL;
        out=perf_rs_replace(out,anchor,
            "uniform sampler2DShadow _perf_rs_unique_shadow;\n#define SAMPLER_INDEX_NORMAL0 0");
        if (!out) return NULL;
    }
    if (strstr(out, "sampler2D(") || strstr(out, "samplerCube(") ||
        strstr(out, "sampler2DShadow(") || strstr(out, "sampler2DArrayShadow(")) {
        free(out); return NULL;
    }
    return out;
}

typedef struct {
    GLint location, original, block, offset, unit;
    GLenum target, binding;
    GLuint64 handle, cached_handle;
    bindless_handle_rec *cached;
    unsigned char handle_value;
    GLenum type;
} perf_rs_sampler;
typedef struct {
    GLint block, first, end;
} perf_rs_range;
typedef struct {
    GLuint original, program;
    GLint count, texture_limit, blocks[8], bytes[8], proxy_block, proxy_offset;
    GLint locations[PERF_RS_VALUES], sizes[PERF_RS_VALUES];
    float values[PERF_RS_VALUES][4];
    uint64_t dirty;
    uint64_t sampler_known, sampler_handles, sampler_values[PERF_RS_VALUES];
    perf_rs_sampler samplers[PERF_RS_SLOTS];
    GLint first[8];
    GLint ui_textures;
    GLint alpha_depth;
    GLint npr;
    uint64_t byte_fallbacks;
    GLuint missing_buffer;
    GLint missing_binding;
    GLintptr missing_offset;
    GLsizeiptr missing_size;
    GLint missing_mapped, missing_gpu;
    GLint range_count;
    perf_rs_range ranges[PERF_RS_SLOTS+1];
    GLint indirect_shadow, drawid_original, drawid_private, perdraw_block, perdraw_stride;
    GLint sampler_units[PERF_RS_SLOTS];
    bindless_map_rec *maps[8];
} perf_rs_program;
static perf_rs_program *g_perf_rs_programs[65536], *g_perf_rs_active;
static HGLRC g_perf_rs_context;
static GLuint g_perf_rs_saved_textures[PERF_RS_SLOTS];
static GLuint g_perf_rs_saved_samplers[PERF_RS_SLOTS];
static GLint g_perf_rs_saved_active;
static uint32_t g_perf_rs_changed_textures, g_perf_rs_changed_samplers;
/* Application bindings queried from GL, not texture contents. Temporary
   bindings are still restored after each draw. A target change or any
   application mutation of the unit forces a fresh query. */
static uint32_t g_perf_rs_units_known;
static GLenum g_perf_rs_unit_binding[PERF_RS_SLOTS];
static GLuint g_perf_rs_unit_textures[PERF_RS_SLOTS];
static GLuint g_perf_rs_unit_samplers[PERF_RS_SLOTS];

static void perf_rs_invalidate_units(GLuint first, GLsizei count)
{
    if (count<=0 || first>=64+PERF_RS_SLOTS) return;
    uint64_t end=(uint64_t)first+(unsigned)count;
    if (end<=64) return;
    unsigned lo=first>64 ? first-64 : 0;
    unsigned hi=end<64+PERF_RS_SLOTS ? (unsigned)end-64 : PERF_RS_SLOTS;
    uint32_t mask=(uint32_t)(((1ULL<<hi)-1)^((1ULL<<lo)-1));
    g_perf_rs_units_known&=~mask;
}
/* Private state persists only within one application multi-draw. Original
   DrawID writes target the application program explicitly during that scope. */
static int g_perf_rs_batch_on=1;
static int g_perf_rs_batch_program_on=1;
static unsigned long long g_perf_rs_batch_scopes, g_perf_rs_batch_draws;
static perf_rs_program *g_perf_rs_batch;
static int g_perf_rs_batch_program_bound, g_perf_rs_batch_validated;
static uint32_t g_perf_rs_batch_known;
static GLuint g_perf_rs_batch_textures[PERF_RS_SLOTS];
static GLuint g_perf_rs_batch_samplers[PERF_RS_SLOTS];
static bindless_handle_rec *g_perf_rs_handle_index[2048];
static struct {
    GLuint texture;
    unsigned get_calls;
    GLint target;
} g_perf_rs_handle_targets[BINDLESS_HANDLE_MAX];

static struct {
    HGLRC (WINAPI *context)(void);
    void (WINAPI *get)(GLenum, GLint *);
    void (WINAPI *get_i)(GLenum, GLuint, GLint *);
    void (WINAPI *get_i64)(GLenum, GLuint, GLint64 *);
    void (WINAPI *get_tex)(GLuint, GLenum, GLint *);
    void (WINAPI *active)(GLenum);
    void (WINAPI *bind_tex)(GLenum, GLuint);
    void (WINAPI *bind_sampler)(GLuint, GLuint);
    void (WINAPI *use)(GLuint);
    void (WINAPI *u4)(GLuint, GLint, GLsizei, const float *);
    void (WINAPI *u1)(GLuint, GLint, GLint);
    void (WINAPI *get_u4)(GLuint, GLint, float *);
    void (WINAPI *get_u1)(GLuint, GLint, GLint *);
    void (WINAPI *program_iv)(GLuint, GLenum, GLint *);
    void (WINAPI *uniform)(GLuint, GLuint, GLsizei, GLsizei *, GLint *, GLenum *, char *);
    void (WINAPI *uniform_iv)(GLuint, GLsizei, const GLuint *, GLenum, GLint *);
    void (WINAPI *resource_iv)(GLuint, GLenum, GLuint, GLsizei, const GLenum *,
                              GLsizei, GLsizei *, GLint *);
    void (WINAPI *indices)(GLuint, GLsizei, const char *const *, GLuint *);
    GLint (WINAPI *location)(GLuint, const char *);
    void (WINAPI *block_iv)(GLuint, GLuint, GLenum, GLint *);
    GLuint (WINAPI *create_program)(void);
    GLuint (WINAPI *create_shader)(GLenum);
    void (WINAPI *source)(GLuint, GLsizei, const char *const *, const GLint *);
    void (WINAPI *compile)(GLuint);
    void (WINAPI *shader_iv)(GLuint, GLenum, GLint *);
    void (WINAPI *attach)(GLuint, GLuint);
    void (WINAPI *link)(GLuint);
    void (WINAPI *delete_shader)(GLuint);
    void (WINAPI *delete_program)(GLuint);
    void (WINAPI *bind_multi_tex)(GLenum, GLenum, GLuint);
} g_perf_rs_gl;

static PROC perf_rs_proc(const char *name)
{
    PROC p = trace_resolve(name);
    if (!p) p = context_gl_export(name);
    if (!p && g_real) p = GetProcAddress(g_real, name);
    return p;
}

static int perf_rs_api(void)
{
    if (g_perf_rs_gl.get) return 1;
#define RS_API(member, name) do { \
    *(PROC *)&g_perf_rs_gl.member = perf_rs_proc(name); \
    if (!g_perf_rs_gl.member) goto missing; \
} while (0)
    RS_API(get, "glGetIntegerv"); RS_API(get_i, "glGetIntegeri_v");
    RS_API(context, "wglGetCurrentContext");
    RS_API(get_i64, "glGetInteger64i_v"); RS_API(get_tex, "glGetTextureParameteriv");
    RS_API(active, "glActiveTexture"); RS_API(bind_tex, "glBindTexture");
    RS_API(bind_sampler, "glBindSampler"); RS_API(use, "glUseProgram");
    RS_API(u4, "glProgramUniform4fv"); RS_API(u1, "glProgramUniform1i");
    RS_API(get_u4, "glGetUniformfv"); RS_API(get_u1, "glGetUniformiv");
    RS_API(program_iv, "glGetProgramiv"); RS_API(uniform, "glGetActiveUniform");
    RS_API(uniform_iv, "glGetActiveUniformsiv"); RS_API(indices, "glGetUniformIndices");
    RS_API(resource_iv, "glGetProgramResourceiv");
    RS_API(location, "glGetUniformLocation"); RS_API(block_iv, "glGetActiveUniformBlockiv");
    RS_API(create_program, "glCreateProgram"); RS_API(create_shader, "glCreateShader");
    RS_API(source, "glShaderSource"); RS_API(compile, "glCompileShader");
    RS_API(shader_iv, "glGetShaderiv"); RS_API(attach, "glAttachShader");
    RS_API(link, "glLinkProgram"); RS_API(delete_shader, "glDeleteShader");
    RS_API(delete_program, "glDeleteProgram");
#undef RS_API
    *(PROC *)&g_perf_rs_gl.bind_multi_tex=perf_rs_proc("glBindMultiTextureEXT");
    return 1;
missing:
    memset(&g_perf_rs_gl, 0, sizeof g_perf_rs_gl);
    return 0;
}

static void perf_rs_forget(GLuint program)
{
    if (program >= 65536) return;
    perf_rs_program *p = g_perf_rs_programs[program];
    if (!p) return;
    if (p->program && g_perf_rs_gl.delete_program)
        g_perf_rs_gl.delete_program(p->program);
    free(p); g_perf_rs_programs[program] = NULL;
}

static int perf_rs_member(GLuint program, const char *name, GLint *block, GLint *offset)
{
    GLuint index = ~0u;
    g_perf_rs_gl.indices(program, 1, &name, &index);
    if (index == ~0u) return 0;
    g_perf_rs_gl.uniform_iv(program, 1, &index, 0x8A3A, block);
    g_perf_rs_gl.uniform_iv(program, 1, &index, 0x8A3B, offset);
    return *block >= 0 && *block < 8 && *offset >= 0 && *offset <= 2040;
}

static int perf_rs_sampler_type(GLenum type, GLenum *target, GLenum *binding)
{
    switch (type) {
    case 0x8B5E: case 0x8B62: *target=0xDE1; *binding=0x8069; return 1;
    case 0x8B5F: *target=0x806F; *binding=0x806A; return 1;
    case 0x8B60: *target=0x8513; *binding=0x8514; return 1;
    case 0x8DD0: *target=0x8C2A; *binding=0x8C2C; return 1;
    case 0x8DC4: *target=0x8C1A; *binding=0x8C1D; return 1;
    default: return 0;
    }
}

static int perf_rs_interfaces_equal(GLuint original, GLuint variant)
{
    typedef void (WINAPI *iv_t)(GLuint,GLenum,GLenum,GLint *);
    typedef void (WINAPI *name_t)(GLuint,GLenum,GLuint,GLsizei,GLsizei *,char *);
    typedef GLuint (WINAPI *index_t)(GLuint,GLenum,const char *);
    typedef void (WINAPI *values_t)(GLuint,GLenum,GLuint,GLsizei,const GLenum *,GLsizei,GLsizei *,GLint *);
    static iv_t iv;
    static name_t name;
    static index_t index;
    if (!iv) iv=(iv_t)perf_rs_proc("glGetProgramInterfaceiv");
    if (!name) name=(name_t)perf_rs_proc("glGetProgramResourceName");
    if (!index) index=(index_t)perf_rs_proc("glGetProgramResourceIndex");
    values_t values=g_perf_rs_gl.resource_iv;
    if (!iv || !name || !index || !values) return 0;
    const GLenum interfaces[]={0x92E3,0x92E4,0x92E6};
    for (unsigned t=0;t<3;t++) {
        GLenum iface=interfaces[t]; GLint a=0,b=0;
        iv(original,iface,0x92F5,&a); iv(variant,iface,0x92F5,&b);
        if (a!=b) return 0;
        const GLenum attrib[]={0x92FA,0x92FB,0x930E};
        const GLenum blocks[]={0x9302,0x9303};
        const GLenum *fields=t==2?blocks:attrib;
        int n=t==2?2:3;
        for (int i=0;i<a;i++) {
            char text[256]; GLint va[3],vb[3];
            name(original,iface,i,sizeof text,NULL,text);
            GLuint other=index(variant,iface,text);
            if (other==~0u) return 0;
            values(original,iface,i,n,fields,n,NULL,va);
            values(variant,iface,other,n,fields,n,NULL,vb);
            if (memcmp(va,vb,(size_t)n*sizeof(GLint))) return 0;
        }
    }
    return 1;
}

static int perf_rs_build_ranges(perf_rs_program *p)
{
    unsigned char needed[8][2048]={{0}};
    if (p->proxy_block>=0) {
        if (p->proxy_block>=8 || p->proxy_offset<0 || p->proxy_offset>2044) return 0;
        memset(needed[p->proxy_block]+p->proxy_offset,1,4);
    }
    for (int i=0;i<p->count;i++) {
        const perf_rs_sampler *s=&p->samplers[i];
        if (s->block>=0) {
            if (s->block>=8 || s->offset<0 || s->offset>2040) return 0;
            memset(needed[s->block]+s->offset,1,8);
        }
    }
    p->range_count=0;
    for (int b=0;b<8;b++) {
        int at=p->first[b];
        while (at<p->bytes[b]) {
            if (!needed[b][at]) { at++; continue; }
            int first=at;
            while (at<p->bytes[b] && needed[b][at]) at++;
            if (p->range_count>=PERF_RS_SLOTS+1) return 0;
            p->ranges[p->range_count++]=(perf_rs_range){b,first,at};
        }
    }
    return 1;
}

static int perf_rs_uniform_layout_equal(GLuint original, GLuint index,
                                        GLuint variant, GLuint other)
{
    /* GL_UNIFORM properties match TYPE, SIZE, OFFSET, ARRAY_STRIDE,
       MATRIX_STRIDE and IS_ROW_MAJOR from glGetActiveUniformsiv. */
    const GLenum fields[] = {0x92FA,0x92FB,0x92FC,0x92FE,0x92FF,0x9300};
    GLint a[6], b[6];
    GLsizei a_count=0, b_count=0;
    g_perf_rs_gl.resource_iv(original,0x92E1,index,6,fields,6,&a_count,a);
    g_perf_rs_gl.resource_iv(variant,0x92E1,other,6,fields,6,&b_count,b);
    return a_count==6 && b_count==6 && !memcmp(a,b,sizeof a);
}

typedef struct {
    char name[256];
    GLuint index;
} perf_rs_uniform_name;

static int perf_rs_uniform_name_compare(const void *a, const void *b)
{
    return strcmp(((const perf_rs_uniform_name *)a)->name,
                  ((const perf_rs_uniform_name *)b)->name);
}

static perf_rs_uniform_name *perf_rs_uniform_names(GLuint program, GLint *count)
{
    GLint n=0;
    *count=0;
    g_perf_rs_gl.program_iv(program,0x8B86,&n);
    if (n<=0) return NULL;
    perf_rs_uniform_name *names=calloc((size_t)n,sizeof *names);
    if (!names) return NULL;
    for (GLint i=0;i<n;i++) {
        perf_rs_uniform_name *entry=&names[*count];
        GLsizei length=0; GLint size; GLenum type;
        g_perf_rs_gl.uniform(program,(GLuint)i,sizeof entry->name,&length,
                            &size,&type,entry->name);
        /* Truncated names must use the existing driver lookup. */
        if (length<=0 || length>=(GLsizei)sizeof entry->name-1) continue;
        entry->index=(GLuint)i;
        (*count)++;
    }
    qsort(names,(size_t)*count,sizeof *names,perf_rs_uniform_name_compare);
    return names;
}

static GLuint perf_rs_uniform_name_index(const perf_rs_uniform_name *names,
                                         GLint count, const char *name)
{
    GLint lo=0,hi=count;
    while (lo<hi) {
        GLint mid=lo+(hi-lo)/2;
        int order=strcmp(name,names[mid].name);
        if (!order) return names[mid].index;
        if (order<0) hi=mid; else lo=mid+1;
    }
    return ~0u;
}

static int perf_rs_reflect_members(perf_rs_program *p,
    const perf_rs_uniform_name *names, GLint name_count)
{
    GLint n = 0, limit = p->texture_limit;
    GLuint first_shader=g_prog_shaders[p->original][0];
    for (int i=0;i<PERF_RS_VALUES;i++) p->locations[i] = -1;
    g_perf_rs_gl.program_iv(p->original, 0x8B86, &n);
    for (GLint i=0;i<n;i++) {
        GLuint index = (GLuint)i, other = ~0u;
        char name[256]; GLint size, block; GLenum type;
        g_perf_rs_gl.uniform(p->original,index,sizeof name,NULL,&size,&type,name);
        g_perf_rs_gl.uniform_iv(p->original,1,&index,0x8A3A,&block);
        if (block >= 0) {
            other=perf_rs_uniform_name_index(names,name_count,name);
            if (other==~0u) {
                const char *text=name;
                g_perf_rs_gl.indices(p->program,1,&text,&other);
            }
            if (other == ~0u || block >= 8) return 0;
            if (!perf_rs_uniform_layout_equal(p->original,index,p->program,other)) return 0;
            GLint other_block, binding, other_binding;
            g_perf_rs_gl.uniform_iv(p->program,1,&other,0x8A3A,&other_block);
            g_perf_rs_gl.block_iv(p->original,block,0x8A3F,&binding);
            g_perf_rs_gl.block_iv(p->program,other_block,0x8A3F,&other_binding);
            if (binding != other_binding || binding < 0 || binding >= 64) return 0;
            p->blocks[block] = binding;
        } else if (p->indirect_shadow && type==0x1404 && size==1 &&
                   !strcmp(name,"_amdshim_bindless_draw_id")) {
            p->drawid_original=g_perf_rs_gl.location(p->original,name);
            p->drawid_private=g_perf_rs_gl.location(p->program,name);
            if (p->drawid_original<0 || p->drawid_private<0) return 0;
        } else if (type == 0x8B52) {
            GLint a=g_perf_rs_gl.location(p->original,name), b=g_perf_rs_gl.location(p->program,name);
            if (a<0 || b<0 || size<1 || a>PERF_RS_VALUES-size) return 0;
            for (GLint j=0;j<size;j++) {
                p->locations[a+j]=b+j; p->sizes[a+j]=size-j;
                g_perf_rs_gl.get_u4(p->original,a+j,p->values[a+j]);
                p->dirty |= 1ULL << (a+j);
            }
        } else {
            GLenum target,binding;
            if (size != 1 || (type==0x8B5F && !p->alpha_depth && !p->npr &&
                !(first_shader<65536 && perf_rs_regular_lod0_source(g_shader_src[first_shader].src))) ||
                !perf_rs_sampler_type(type,&target,&binding)) return 0;
        }
    }
    for (int b=0;b<8;b++) p->first[b]=2048;
    p->proxy_block=-1;
    int grass=first_shader<65536 && perf_rs_grass_source(g_shader_src[first_shader].src);
    /* Grass lowering does not substitute the reflection-proxy path. */
    if (!p->ui_textures && !p->alpha_depth && !grass && !p->indirect_shadow) {
        if (!perf_rs_member(p->original,"g_reflection_proxy_count",&p->proxy_block,&p->proxy_offset)) return 0;
        p->first[p->proxy_block]=p->proxy_offset;
        p->bytes[p->proxy_block] = p->proxy_offset+4;
    }
    g_perf_rs_gl.program_iv(p->program,0x8B86,&n);
    for (GLint i=0;i<n;i++) {
        GLuint index=(GLuint)i;
        char name[256]; GLint size,block; GLenum type,target,binding;
        g_perf_rs_gl.uniform(p->program,index,sizeof name,NULL,&size,&type,name);
        g_perf_rs_gl.uniform_iv(p->program,1,&index,0x8A3A,&block);
        if (block >= 0 || type == 0x8B52 || (p->indirect_shadow && type==0x1404 &&
            !strcmp(name,"_amdshim_bindless_draw_id"))) continue;
        if (!perf_rs_sampler_type(type,&target,&binding) || size<1 || size>PERF_RS_SLOTS-p->count) return 0;
        GLint location=g_perf_rs_gl.location(p->program,name);
        for (GLint j=0;j<size;j++) {
            perf_rs_sampler *s=&p->samplers[p->count];
            s->location=location+j; s->original=-1; s->block=-1;
            s->target=target; s->binding=binding; s->type=type;
            char member[128]; member[0]=0;
            if (p->ui_textures) {
                if (strcmp(name,"_perf_rs_ui[0]") || type!=0x8B5E || size!=p->ui_textures) return 0;
                s->original=g_perf_rs_gl.location(p->original,"g_constants[0]")+j;
                if (s->original<0 || s->original>=PERF_RS_VALUES || p->locations[s->original]<0) return 0;
                s->block=-2; /* Packed uint64 in the original vec4's xy bits. */
            } else if (p->indirect_shadow && !strcmp(name,"_perf_rs_indirect_opacity")) {
                if (type!=0x8B5E || size!=1) return 0;
                GLint next_block,next_offset;
                if (!perf_rs_member(p->original,"g_draws[0].m_opacity_map",&p->perdraw_block,&next_offset) ||
                    next_offset!=96 ||
                    !perf_rs_member(p->original,"g_draws[1].m_opacity_map",&next_block,&next_offset) ||
                    next_block!=p->perdraw_block || next_offset!=208) return 0;
                p->perdraw_stride=112;
                strcpy(member,"g_draws[0].m_opacity_map");
            } else if (p->alpha_depth &&
                       (!strcmp(name,"_perf_rs_opacity") || !strcmp(name,"_perf_rs_opacity1"))) {
                if (type!=0x8B5E || size!=1) return 0;
                strcpy(member,"_amdshim_map_handle_pairs[0]");
            } else if (!strcmp(name,"_perf_rs_unique_shadow")) {
                if (type!=0x8B62 || size!=1) return 0;
                strcpy(member,"g_unique_shadow_map_handle");
            } else if (!strncmp(name,"_perf_rs_npr_",13)) {
                int k=atoi(name+13);
                if (!p->npr || k<0 || k>18 || type!=0x8B5E || size!=1) return 0;
                strcpy(member,k==18?"_amdshim_map_handle_last":"_amdshim_map_handle_pairs[0]");
            } else if (!strncmp(name,"_perf_rs_foliage_",17)) {
                int k=atoi(name+17);
                if (perf_rs_foliage_source(g_shader_src[first_shader].src)!=1 ||
                    (k!=4 && k!=8 && k!=16 && k!=18) || type!=0x8B5E || size!=1) return 0;
                strcpy(member,k==18?"_amdshim_map_handle_last":"_amdshim_map_handle_pairs[0]");
            } else if (!strcmp(name,"_perf_rs_maps[0]")) {
                strcpy(member,j==18?"_amdshim_map_handle_last":"_amdshim_map_handle_pairs[0]");
            } else if (!strncmp(name,"_perf_rs_handle_",16)) {
                static const char *handles[] = {
                    "g_wind_perturbation_map_handle","g_water_normal_map_handle",
                    "g_planar_reflection_map_handle","g_screen_shadow_map_handle",
                    "g_ssao_map_handle","g_local_shadow_map_handle"
                };
                int k=atoi(name+16);
                if (k<0 || k>5) return 0;
                strcpy(member,handles[k]);
            } else if (!strncmp(name,"_perf_rs_shadow",15)) {
                snprintf(member,sizeof member,"g_cascaded_shadow_map_handle_%c",name[15]);
            } else {
                s->original=g_perf_rs_gl.location(p->original,name);
                if (s->original<0 || size!=1) return 0;
                g_perf_rs_gl.get_u1(p->program,s->location,&s->unit);
                if (s->unit<0 || s->unit>=limit) return 0;
            }
            if (member[0]) {
                if (!perf_rs_member(p->original,member,&s->block,&s->offset)) return 0;
                if (!strcmp(name,"_perf_rs_opacity")) s->offset+=8*16;
                if (!strcmp(name,"_perf_rs_opacity1")) s->offset+=8*17;
                if (!strncmp(name,"_perf_rs_npr_",13) && atoi(name+13)<18) s->offset+=8*atoi(name+13);
                if (!strncmp(name,"_perf_rs_foliage_",17) && atoi(name+17)<18) s->offset+=8*atoi(name+17);
                if (!strcmp(name,"_perf_rs_maps[0]") && j<18) s->offset+=8*j;
                if (s->offset<p->first[s->block]) p->first[s->block]=s->offset;
                if (s->offset+8>p->bytes[s->block]) p->bytes[s->block]=s->offset+8;
            }
            g_perf_rs_gl.u1(p->program,s->location,64+p->count);
            p->sampler_units[p->count]=64+p->count;
            p->count++;
        }
    }
    return p->count > 0 && perf_rs_build_ranges(p);
}

static int perf_rs_reflect(perf_rs_program *p)
{
    GLint limit=0,name_count=0;
    g_perf_rs_gl.get(0x8B4D,&limit);
    if (limit<64+PERF_RS_SLOTS) return 0;
    p->texture_limit=limit;
    if (!perf_rs_interfaces_equal(p->original,p->program)) return 0;
    /* Large arrays of structs otherwise repeat the driver's name search. */
    perf_rs_uniform_name *names=perf_rs_uniform_names(p->program,&name_count);
    int ok=perf_rs_reflect_members(p,names,name_count);
    free(names);
    return ok;
}

static void perf_rs_build(GLuint program)
{
    if (!g_perf_regular_samplers_on || program>=65536 ||
        g_prog_shader_count[program]!=2 || !perf_rs_api()) return;
    perf_rs_program *p=g_perf_rs_programs[program];
    if (!p || p->program) return;
    HGLRC context=g_perf_rs_gl.context();
    if (!context || (g_perf_rs_context && context!=g_perf_rs_context)) {
        perf_rs_forget(program); return;
    }
    g_perf_rs_context=context;
    GLint ok=0;
    g_perf_rs_gl.program_iv(program,0x8B82,&ok);
    if (!ok) return;
    g_perf_rs_gl.program_iv(program,0x8C83,&ok);
    if (ok) return;
    g_perf_rs_gl.program_iv(program,0x8258,&ok);
    if (ok) return;
    char *sources[2]={NULL,NULL};
    GLenum types[2]={0,0};
    for (int i=0;i<2;i++) {
        GLuint shader=g_prog_shaders[program][i];
        if (shader>=65536) goto cleanup;
        types[i]=g_shader_types[shader];
        if (types[i]!=0x8B31 && types[i]!=0x8B30) goto cleanup;
        sources[i]=perf_rs_source(g_shader_src[shader].src);
        if (!sources[i]) goto cleanup;
    }
    if (types[0]==types[1]) goto cleanup;
    p->original=program; p->program=g_perf_rs_gl.create_program();
    for (int i=0;i<2;i++) {
        GLuint shader=g_perf_rs_gl.create_shader(types[i]);
        const char *src=sources[i];
        g_perf_rs_gl.source(shader,1,&src,NULL); g_perf_rs_gl.compile(shader);
        g_perf_rs_gl.shader_iv(shader,0x8B81,&ok);
        if (ok) g_perf_rs_gl.attach(p->program,shader);
        g_perf_rs_gl.delete_shader(shader);
        if (!ok) { perf_rs_forget(program); goto cleanup; }
    }
    g_perf_rs_gl.link(p->program);
    g_perf_rs_gl.program_iv(p->program,0x8B82,&ok);
    if (!ok || !perf_rs_reflect(p)) { perf_rs_forget(program); p=NULL; }
    if (p) for (int i=0;i<p->count;i++) {
        perf_rs_sampler *s=&p->samplers[i];
        int k=s->original;
        if (k>=0 && k<PERF_RS_VALUES && (p->sampler_known&(1ULL<<k))) {
            s->handle_value=(p->sampler_handles&(1ULL<<k))!=0;
            s->handle=p->sampler_values[k]; s->unit=(GLint)s->handle;
        }
    }
cleanup:
    free(sources[0]); free(sources[1]);
    if (g_perf_rs_programs[program] && !g_perf_rs_programs[program]->program)
        perf_rs_forget(program);
}

static void perf_rs_link(GLuint program)
{
    perf_rs_forget(program);
    if (!g_perf_regular_samplers_on || program>=65536 ||
        g_prog_shader_count[program]!=2) return;
    GLuint first_shader=g_prog_shaders[program][0];
    int ui=perf_rs_ui_texture_count(first_shader<65536?g_shader_src[first_shader].src:NULL);
    int alpha=perf_rs_alpha_depth_source(first_shader<65536?g_shader_src[first_shader].src:NULL);
    int npr=perf_rs_npr_source(first_shader<65536?g_shader_src[first_shader].src:NULL);
    int grass=perf_rs_grass_source(first_shader<65536?g_shader_src[first_shader].src:NULL);
    int foliage=perf_rs_foliage_source(first_shader<65536?g_shader_src[first_shader].src:NULL);
    int indirect=perf_rs_indirect_shadow_source(first_shader<65536?g_shader_src[first_shader].src:NULL);
    int wind_depth=perf_rs_wind_depth_source(first_shader<65536?g_shader_src[first_shader].src:NULL);
    if (foliage==2) alpha=1;
    if (wind_depth) alpha=1;
    for (int i=0;i<2;i++) {
        GLuint shader=g_prog_shaders[program][i];
        const char *src=shader<65536?g_shader_src[shader].src:NULL;
        if (indirect) {
            if (!perf_rs_indirect_shadow_source(src)) return;
            continue;
        }
        if (wind_depth) {
            if (!perf_rs_wind_depth_source(src)) return;
            continue;
        }
        if (foliage) {
            if (perf_rs_foliage_source(src)!=foliage) return;
            continue;
        }
        if (grass) {
            if (!perf_rs_grass_source(src)) return;
            continue;
        }
        if (ui) {
            if (perf_rs_ui_texture_count(src)!=ui) return;
            continue;
        }
        if (alpha) {
            if (!perf_rs_alpha_depth_source(src)) return;
            continue;
        }
        if (npr) {
            if (!perf_rs_npr_source(src)) return;
            continue;
        }
        if (perf_rs_regular_lod0_source(src)) continue;
        if (!src || !strstr(src,"_amdshim_map_handle_pairs") ||
            !strstr(src,"#define HAVE_VERTEX_COLOR 0") ||
            !strstr(src,"#define LIGHTING_TYPE 1") ||
            !strstr(src,"#define DEPTH_ONLY 0") ||
            !strstr(src,"#define EFFECT_SHADER 0")) return;
    }
    /* GL shader/program names are shared. Wait until actual rendering before
       allocating private objects, as the existing GPU depth helper does. */
    g_perf_rs_programs[program]=calloc(1,sizeof(perf_rs_program));
    if (g_perf_rs_programs[program]) {
        g_perf_rs_programs[program]->ui_textures=ui;
        g_perf_rs_programs[program]->alpha_depth=alpha;
        g_perf_rs_programs[program]->npr=npr;
        g_perf_rs_programs[program]->indirect_shadow=indirect;
    }
}

static void perf_rs_uniform4(GLuint program, GLint location, GLsizei count, const float *values)
{
    if (program>=65536 || location<0 || count<=0 || !values) return;
    perf_rs_program *p=g_perf_rs_programs[program];
    if (!p || location>=PERF_RS_VALUES || p->sizes[location]<=0) return;
    if (count>p->sizes[location]) count=p->sizes[location];
    for (int i=0;i<count;i++) {
        int k=location+i;
        if (p->locations[k]<0) continue;
        if (memcmp(p->values[k],values+4*i,16)) {
            memcpy(p->values[k],values+4*i,16); p->dirty|=1ULL<<k;
        }
    }
}

static void perf_rs_uniform_sampler(GLuint program, GLint location, GLuint64 value, int handle)
{
    if (program>=65536 || location<0) return;
    perf_rs_program *p=g_perf_rs_programs[program];
    if (!p) return;
    if (location<PERF_RS_VALUES) {
        p->sampler_known|=1ULL<<location;
        if (handle) p->sampler_handles|=1ULL<<location;
        else p->sampler_handles&=~(1ULL<<location);
        p->sampler_values[location]=value;
    }
    for (int i=0;i<p->count;i++) {
        perf_rs_sampler *s=&p->samplers[i];
        if (s->original != location) continue;
        s->handle_value=(unsigned char)handle; s->handle=value; s->unit=(GLint)value;
    }
}

static bindless_handle_rec *perf_rs_find_handle(GLuint64 handle)
{
    GLuint64 key=handle^(handle>>32);
    key^=key>>16;
    unsigned index=(unsigned)((key*0x9e3779b97f4a7c15ULL)>>53);
    bindless_handle_rec *h=g_perf_rs_handle_index[index];
    if (!h || h->handle!=handle)
        g_perf_rs_handle_index[index]=h=bindless_handle_find(handle);
    return h;
}

static GLint perf_rs_handle_target(bindless_handle_rec *h)
{
    unsigned index=(unsigned)(h-g_bindless_handles);
    if (g_perf_rs_handle_targets[index].texture!=h->texture ||
        g_perf_rs_handle_targets[index].get_calls!=h->get_calls ||
        !g_perf_rs_handle_targets[index].target) {
        GLint target=0;
        g_perf_rs_gl.get_tex(h->texture,0x1006,&target);
        g_perf_rs_handle_targets[index].texture=h->texture;
        g_perf_rs_handle_targets[index].get_calls=h->get_calls;
        g_perf_rs_handle_targets[index].target=target;
    }
    return g_perf_rs_handle_targets[index].target;
}

#include <smmintrin.h>

static __attribute__((target("sse4.1"), always_inline)) inline void
perf_rs_wc_read_lanes(const unsigned char *source, size_t size, unsigned char *dst)
{
    while (size) {
        uintptr_t aligned = (uintptr_t)source & ~(uintptr_t)15;
        size_t skip = (uintptr_t)source - aligned;
        size_t take = 16 - skip;
        if (take > size) take = size;
        __m128i value = _mm_stream_load_si128((__m128i *)aligned);
        unsigned char lane[16];
        _mm_storeu_si128((__m128i *)lane, value);
        memcpy(dst, lane + skip, take);
        source += take; dst += take; size -= take;
    }
}

static __attribute__((target("sse4.1"), noinline)) void
perf_rs_wc_read(const unsigned char *source, size_t size, unsigned char *dst)
{
    /* Order WC reads against earlier CPU writes; retain no bytes across calls. */
    _mm_mfence();
    perf_rs_wc_read_lanes(source,size,dst);
    _mm_mfence();
}

typedef struct {
    const unsigned char *source;
    unsigned char *dst;
    size_t size;
    bindless_map_rec *map;
} perf_rs_wc_request;

static __attribute__((target("sse4.1"), noinline)) void
perf_rs_wc_read_batch(const perf_rs_wc_request *reads, int count)
{
    /* All requests belong to this draw; finish before interpreting any bytes. */
    _mm_mfence();
    for (int i=0;i<count;i++)
        perf_rs_wc_read_lanes(reads[i].source,reads[i].size,reads[i].dst);
    _mm_mfence();
    for (int i=0;i<count;i++)
        perf_mapped_read_store(reads[i].map,reads[i].source,reads[i].size,reads[i].dst);
#ifdef FGO_MAPPED_LIFETIME_AUDIT
    for (int i=0;i<count;i++)
        perf_mapped_audit_note(reads[i].map,reads[i].source,reads[i].size,reads[i].dst);
#endif
}

static int perf_rs_wc_eligible(bindless_map_rec *m, const unsigned char *source,
                               size_t size)
{
    if (!m->sampler_wc_read) {
        MEMORY_BASIC_INFORMATION info;
        m->sampler_wc_read = 2;
        if (__builtin_cpu_supports("sse4.1") &&
            VirtualQuery(m->ptr, &info, sizeof info) &&
            info.Protect == (PAGE_READWRITE | PAGE_WRITECOMBINE) &&
            (size_t)m->length <= info.RegionSize -
                ((uintptr_t)m->ptr - (uintptr_t)info.BaseAddress))
            m->sampler_wc_read = 1;
    }
    if (m->sampler_wc_read != 1) return 0;
    /* Aligned vector loads may include neighboring bytes, but never map edges. */
    uintptr_t first = (uintptr_t)source & ~(uintptr_t)15;
    size_t tail = (-(uintptr_t)(source + size)) & 15;
    if (first < (uintptr_t)m->ptr ||
        (size_t)(source - m->ptr) + size + tail > (size_t)m->length) return 0;
    return 1;
}

static int perf_rs_wc_copy(bindless_map_rec *m, const unsigned char *source,
                           size_t size, void *dst)
{
    if (!perf_rs_wc_eligible(m,source,size)) return 0;
    perf_rs_wc_read(source,size,dst);
    return 1;
}

static int perf_rs_queue_mapped_read(bindless_map_rec *m, GLintptr offset,
                                    GLsizeiptr size, void *data,
                                    perf_rs_wc_request *reads, int *count)
{
    if (*count>=PERF_RS_SLOTS+1 || size<=0 ||
        !(g_perf_pointer_shadow_on || g_perf_cpu_shadow_on) || !m->ptr ||
        !(m->access&0x0002) || offset<m->offset || size>m->length ||
        offset-m->offset>m->length-size) return 0;
    const unsigned char *source=m->ptr+(size_t)(offset-m->offset);
    if (!perf_rs_wc_eligible(m,source,(size_t)size)) return 0;
    if (perf_mapped_read_copy(m,source,(size_t)size,data)) return 1;
    reads[(*count)++]=(perf_rs_wc_request){source,data,(size_t)size,m};
    return 1;
}

static int perf_rs_mapped_copy(bindless_map_rec *m, GLuint buffer, GLintptr offset,
                              GLsizeiptr size, void *data)
{
    if ((g_perf_pointer_shadow_on || g_perf_cpu_shadow_on) && m->ptr &&
        (m->access&0x0002) && offset>=m->offset && size<=m->length &&
        offset-m->offset<=m->length-size) {
        const unsigned char *source=m->ptr+(size_t)(offset-m->offset);
        if (perf_mapped_read_copy(m,source,(size_t)size,data)) return 1;
        if (size>16 && perf_rs_wc_copy(m,source,(size_t)size,data)) {
            perf_mapped_read_store(m,source,(size_t)size,data);
#ifdef FGO_MAPPED_LIFETIME_AUDIT
            perf_mapped_audit_note(m,source,(size_t)size,data);
#endif
            g_perf_pointer_shadow_reads++;
            return 1;
        }
        /* Keep these reads current; fixed sizes avoid a CRT call per handle. */
        if (size==8) memcpy(data,source,8);
        else if (size==16) memcpy(data,source,16);
        else if (size==4) memcpy(data,source,4);
        else memcpy(data,source,(size_t)size);
        perf_mapped_read_store(m,source,(size_t)size,data);
#ifdef FGO_MAPPED_LIFETIME_AUDIT
        perf_mapped_audit_note(m,source,(size_t)size,data);
#endif
        g_perf_pointer_shadow_reads++;
        return 1;
    }
    return perf_pointer_shadow_copy(buffer,offset,size,data);
}

static void perf_rs_restore_units(perf_rs_program *p)
{
    for (uint32_t mask=g_perf_rs_changed_textures;mask;mask&=mask-1) {
        unsigned i=(unsigned)__builtin_ctz(mask);
        if (g_perf_rs_gl.bind_multi_tex)
            g_perf_rs_gl.bind_multi_tex(0x84C0+64+i,p->samplers[i].target,g_perf_rs_saved_textures[i]);
        else {
            g_perf_rs_gl.active(0x84C0+64+i);
            g_perf_rs_gl.bind_tex(p->samplers[i].target,g_perf_rs_saved_textures[i]);
        }
    }
    for (uint32_t mask=g_perf_rs_changed_samplers;mask;mask&=mask-1) {
        unsigned i=(unsigned)__builtin_ctz(mask);
        g_perf_rs_gl.bind_sampler(64+i,g_perf_rs_saved_samplers[i]);
    }
    if (!g_perf_rs_gl.bind_multi_tex) g_perf_rs_gl.active(g_perf_rs_saved_active);
    g_perf_rs_changed_textures=g_perf_rs_changed_samplers=0;
    g_perf_rs_batch_known=0;
}

static void perf_rs_indirect_batch_begin(GLsizei count)
{
    if (!g_perf_rs_batch_on || !g_perf_regular_samplers_on || count<2 ||
        g_perf_rs_active || g_perf_rs_batch || g_current_program>=65536 ||
        !g_perf_rs_gl.bind_multi_tex) return;
    perf_rs_program *p=g_perf_rs_programs[g_current_program];
    if (!p || !p->program || !p->indirect_shadow) return;
    g_perf_rs_batch=p;
    g_perf_rs_batch_program_bound=g_perf_rs_batch_validated=0;
    g_perf_rs_batch_scopes++;
    g_perf_rs_batch_known=0;
    g_perf_rs_changed_textures=g_perf_rs_changed_samplers=0;
}

static void perf_rs_indirect_batch_end(void)
{
    if (!g_perf_rs_batch) return;
    if (g_perf_rs_batch_program_bound) g_perf_rs_gl.use(g_perf_rs_batch->original);
    g_perf_rs_batch_program_bound=g_perf_rs_batch_validated=0;
    perf_rs_restore_units(g_perf_rs_batch);
    g_perf_rs_batch=NULL;
}

static int perf_rs_indirect_drawid(GLint location, GLint draw_id)
{
    if (!g_perf_rs_batch || !g_perf_rs_batch_program_bound) return 0;
    g_perf_rs_gl.u1(g_perf_rs_batch->original,location,draw_id);
    return 1;
}

static int perf_rs_begin_draw(GLint draw_id)
{
    if (!g_perf_regular_samplers_on || g_current_program>=65536) return 0;
    perf_rs_program *p=g_perf_rs_programs[g_current_program];
    if (!p || g_perf_rs_active) return 0;
    /* Keep early UI draws on the original until the existing model variant
       has started; do not consume extra GL object names during startup. */
    if (p->ui_textures && !g_perf_rs_draws) return 0;
    /* Mixed default-uniform handle assignment differs on this AMD driver.
       Keep that mode on the original program; UBO handles are unaffected. */
    if (p->sampler_handles) { g_perf_rs_handle_fallbacks++; goto fallback; }
    if (!p->program) {
        perf_rs_build(g_current_program);
        p=g_perf_rs_programs[g_current_program];
        if (!p || !p->program) return 0;
    }
    int batch=g_perf_rs_batch==p;
    if (!batch || !g_perf_rs_batch_validated) {
        GLint current=0, tf=0;
        if (g_perf_rs_state_cache_on && g_current_program_valid) {
            current=(GLint)g_current_program;
            g_perf_rs_program_query_skips++;
        } else {
            g_perf_rs_gl.get(0x8B8D,&current);
        }
        g_perf_rs_gl.get(0x8E24,&tf);
        if ((GLuint)current!=p->original || tf) { g_perf_rs_rejects[0]++; goto fallback; }
        /* No application state change can occur between these subdraws. */
        if (batch && g_perf_rs_batch_program_on) g_perf_rs_batch_validated=1;
    }
    if (p->indirect_shadow) {
        if (draw_id<0) g_perf_rs_gl.get_u1(p->original,p->drawid_original,&draw_id);
        if (draw_id<0 || draw_id>=512 || p->perdraw_stride!=112) {
            g_perf_rs_rejects[0]++; goto fallback;
        }
    }
    unsigned char bytes[8][2048];
    perf_rs_wc_request pending[PERF_RS_SLOTS+1];
    int pending_count=0;
    for (int b=0;b<8;b++) {
        if (!p->bytes[b]) continue;
        GLint buffer=0; GLint64 offset=0,length=0;
        unsigned ub=(unsigned)p->blocks[b];
        if (g_perf_rs_state_cache_on && ub<64 && g_ubo_mirror_valid[ub]) {
            buffer=(GLint)g_ubo_buffer[ub];
            offset=(GLint64)g_ubo_offset[ub];
            length=(GLint64)g_ubo_length[ub];
            g_perf_rs_ubo_query_skips++;
        } else {
            g_perf_rs_gl.get_i(0x8A28,p->blocks[b],&buffer);
            g_perf_rs_gl.get_i64(0x8A29,p->blocks[b],&offset);
            g_perf_rs_gl.get_i64(0x8A2A,p->blocks[b],&length);
            g_perf_rs_ubo_query_fallbacks++;
        }
        GLintptr record=p->indirect_shadow && b==p->perdraw_block
            ? (GLintptr)draw_id*p->perdraw_stride : 0;
        if (!buffer || offset<0 || offset>INTPTR_MAX-record-p->bytes[b] ||
            (length && length<record+p->bytes[b])) {
            g_perf_rs_rejects[1]++; goto fallback;
        }
        offset+=record;
        /* Nearby mapped fields share a fresh read. CPU-upload validity and
           sparse mapped blocks still follow the exact reflected ranges. */
        bindless_map_rec *map=p->maps[b];
        if (!map || !map->active || map->buffer!=(GLuint)buffer)
            p->maps[b]=map=bindless_map_find((GLuint)buffer);
        int mapped=map!=NULL;
        int span=p->bytes[b]-p->first[b];
        if (mapped && span>16 && span<=64 && perf_rs_queue_mapped_read(map,
                (GLintptr)offset+p->first[b],span,bytes[b]+p->first[b],
                pending,&pending_count)) continue;
        for (int r=0;r<p->range_count;r++) {
            const perf_rs_range *range=&p->ranges[r];
            if (range->block!=b) continue;
            int first=range->first, size=range->end-first;
            if (mapped && perf_rs_queue_mapped_read(map,(GLintptr)offset+first,
                    size,bytes[b]+first,pending,&pending_count)) continue;
            int copied=mapped
                ? perf_rs_mapped_copy(map,(GLuint)buffer,(GLintptr)offset+first,
                                      size,bytes[b]+first)
                : perf_rs_upload_copy((GLuint)buffer,(GLintptr)offset+first,
                                      size,bytes[b]+first);
            if (!copied) {
                p->byte_fallbacks++; p->missing_buffer=(GLuint)buffer;
                p->missing_binding=p->blocks[b];
                p->missing_offset=(GLintptr)offset+first; p->missing_size=size;
                p->missing_mapped=mapped;
                p->missing_gpu=(GLuint)buffer<(1u<<20) &&
                    (g_perf_rs_gpu_buffers[buffer/8]&(1u<<(buffer&7)))!=0;
                g_perf_rs_missing_buffer=(GLuint)buffer;
                g_perf_rs_rejects[2]++; goto fallback;
            }
        }
    }
    if (pending_count) {
        perf_rs_wc_read_batch(pending,pending_count);
        g_perf_pointer_shadow_reads+=(unsigned)pending_count;
    }
    if (p->proxy_block>=0) {
        float proxies;
        memcpy(&proxies,bytes[p->proxy_block]+p->proxy_offset,4);
        if (proxies!=0.0f) { g_perf_rs_rejects[3]++; goto fallback; }
    }
    int remap_ordinary=0;
    for (int i=0;i<p->count;i++) {
        perf_rs_sampler *s=&p->samplers[i];
        if (s->block!=-1 || s->handle_value) continue;
        if (s->unit<0 || s->unit>=p->texture_limit) {
            g_perf_rs_rejects[6]++; goto fallback;
        }
        if (s->unit>=64 && s->unit<64+PERF_RS_SLOTS) remap_ordinary=1;
    }
    GLuint textures[PERF_RS_SLOTS], samplers[PERF_RS_SLOTS];
    GLint units[PERF_RS_SLOTS];
    uint32_t temporary=0;
    for (int i=0;i<p->count;i++) {
        perf_rs_sampler *s=&p->samplers[i];
        if (s->block==-1 && !s->handle_value && !remap_ordinary) {
            units[i]=s->unit;
            continue;
        }
        GLuint64 handle=s->handle;
        if (s->block>=0) memcpy(&handle,bytes[s->block]+s->offset,8);
        if (s->block==-2) memcpy(&handle,p->values[s->original],8);
        if (s->block>=0 || s->block==-2 || s->handle_value) {
            textures[i]=samplers[i]=0;
            if (!handle) {
                if (p->ui_textures) { g_perf_rs_rejects[4]++; goto fallback; }
            } else {
                if (!s->cached || s->cached_handle!=handle || s->cached->handle!=handle) {
                    s->cached=perf_rs_find_handle(handle); s->cached_handle=handle;
                }
                bindless_handle_rec *h=s->cached;
                if (!h || !h->texture || !h->resident_known || !h->resident_state) {
                    g_perf_rs_rejects[4]++; goto fallback;
                }
                if ((GLenum)perf_rs_handle_target(h)!=s->target) {
                    g_perf_rs_rejects[5]++; goto fallback;
                }
                textures[i]=h->texture; samplers[i]=h->sampler;
            }
        } else {
            GLint texture=0,sampler=0;
            if (s->unit<0 || s->unit>=p->texture_limit) { g_perf_rs_rejects[6]++; goto fallback; }
            g_perf_rs_gl.get_i(s->binding,s->unit,&texture);
            g_perf_rs_gl.get_i(0x8919,s->unit,&sampler);
            textures[i]=(GLuint)texture; samplers[i]=(GLuint)sampler;
        }
        units[i]=64+i;
        for (uint32_t mask=temporary;mask;mask&=mask-1) {
            unsigned j=(unsigned)__builtin_ctz(mask);
            if (p->samplers[j].type==s->type && textures[j]==textures[i] &&
                samplers[j]==samplers[i]) { units[i]=64+(GLint)j; break; }
        }
        if (units[i]==64+i) temporary|=1u<<i;
    }
    for (int k=0;k<PERF_RS_VALUES;k++) {
        if (!(p->dirty&(1ULL<<k))) continue;
        int n=1;
        while (n<p->sizes[k] && (p->dirty&(1ULL<<(k+n))) && p->locations[k+n]==p->locations[k]+n) n++;
        g_perf_rs_gl.u4(p->program,p->locations[k],n,p->values[k]);
        k+=n-1;
    }
    p->dirty=0;
    if (p->indirect_shadow) g_perf_rs_gl.u1(p->program,p->drawid_private,draw_id);
    for (int i=0;i<p->count;i++) {
        if (p->sampler_units[i]!=units[i]) {
            g_perf_rs_gl.u1(p->program,p->samplers[i].location,units[i]);
            p->sampler_units[i]=units[i];
        }
    }
    if (!batch) g_perf_rs_changed_textures=g_perf_rs_changed_samplers=0;
    if (!g_perf_rs_gl.bind_multi_tex) g_perf_rs_gl.get(0x84E0,&g_perf_rs_saved_active);
    for (uint32_t mask=temporary;mask;mask&=mask-1) {
        unsigned i=(unsigned)__builtin_ctz(mask);
        GLint texture=0,sampler=0;
        if (batch && (g_perf_rs_batch_known&(1u<<i))) {
            texture=(GLint)g_perf_rs_batch_textures[i];
            sampler=(GLint)g_perf_rs_batch_samplers[i];
        } else {
            GLenum binding=p->samplers[i].binding;
            if (g_perf_rs_state_cache_on && (g_perf_rs_units_known&(1u<<i)) &&
                g_perf_rs_unit_binding[i]==binding) {
                texture=(GLint)g_perf_rs_unit_textures[i];
                sampler=(GLint)g_perf_rs_unit_samplers[i];
            } else {
                g_perf_rs_gl.get_i(binding,64+i,&texture);
                g_perf_rs_gl.get_i(0x8919,64+i,&sampler);
                g_perf_rs_unit_binding[i]=binding;
                g_perf_rs_unit_textures[i]=(GLuint)texture;
                g_perf_rs_unit_samplers[i]=(GLuint)sampler;
                g_perf_rs_units_known|=1u<<i;
            }
            g_perf_rs_saved_textures[i]=(GLuint)texture;
            g_perf_rs_saved_samplers[i]=(GLuint)sampler;
        }
        if ((GLuint)texture!=textures[i]) {
            g_perf_rs_changed_textures|=1u<<i;
            if (g_perf_rs_gl.bind_multi_tex)
                g_perf_rs_gl.bind_multi_tex(0x84C0+64+i,p->samplers[i].target,textures[i]);
            else {
                g_perf_rs_gl.active(0x84C0+64+i);
                g_perf_rs_gl.bind_tex(p->samplers[i].target,textures[i]);
            }
        }
        if ((GLuint)sampler!=samplers[i]) {
            g_perf_rs_changed_samplers|=1u<<i;
            g_perf_rs_gl.bind_sampler(64+i,samplers[i]);
        }
        if (batch) {
            g_perf_rs_batch_known|=1u<<i;
            g_perf_rs_batch_textures[i]=textures[i];
            g_perf_rs_batch_samplers[i]=samplers[i];
        }
    }
    if (!g_perf_rs_gl.bind_multi_tex) g_perf_rs_gl.active(g_perf_rs_saved_active);
    if (!batch || !g_perf_rs_batch_program_bound) g_perf_rs_gl.use(p->program);
    if (batch && g_perf_rs_batch_program_on) g_perf_rs_batch_program_bound=1;
    g_perf_rs_active=p; g_perf_rs_draws++;
    if (batch) g_perf_rs_batch_draws++;
    if (p->ui_textures) g_perf_rs_ui_draws++;
    return 1;
fallback:
    /* A fallback uses the original shader, including any ordinary samplers it
       may contain.  It must see the application's bindings, not a prior record. */
    if (g_perf_rs_batch==p) {
        if (g_perf_rs_batch_program_bound) g_perf_rs_gl.use(p->original);
        g_perf_rs_batch_program_bound=g_perf_rs_batch_validated=0;
        perf_rs_restore_units(p);
    }
    g_perf_rs_fallbacks++;
    if (p->ui_textures) g_perf_rs_ui_fallbacks++;
    return 0;
}

static int perf_rs_begin(void)
{
    return perf_rs_begin_draw(-1);
}

static void perf_rs_indirect_begin(GLint location, GLint draw_id)
{
    if (!g_perf_regular_samplers_on || location<0 || g_current_program>=65536) return;
    perf_rs_program *p=g_perf_rs_programs[g_current_program];
    if (p && p->indirect_shadow) perf_rs_begin_draw(draw_id);
}

static void perf_rs_end(void)
{
    perf_rs_program *p=g_perf_rs_active;
    if (!p) return;
    if (!g_perf_rs_batch_program_bound) g_perf_rs_gl.use(p->original);
    if (g_perf_rs_batch!=p) perf_rs_restore_units(p);
    g_perf_rs_active=NULL;
}

static void perf_rs_context_change(HGLRC context)
{
    g_perf_rs_units_known=0;
    if (!context || !g_perf_rs_context || context==g_perf_rs_context) return;
    if (g_perf_rs_context) {
        /* Switching share groups cannot preserve this metadata. Disable the
           optimization for this process instead of guessing shared lifetimes. */
        g_perf_regular_samplers_on=0;
        for (unsigned i=0;i<65536;i++) {
            free(g_perf_rs_programs[i]); g_perf_rs_programs[i]=NULL;
        }
    }
    g_perf_rs_context=context;
}

static void perf_rs_context_deleted(HGLRC context)
{
    g_perf_rs_units_known=0;
    if (context!=g_perf_rs_context) return;
    g_perf_regular_samplers_on=0; g_perf_rs_context=NULL;
    for (unsigned i=0;i<65536;i++) {
        free(g_perf_rs_programs[i]); g_perf_rs_programs[i]=NULL;
    }
}

static void WINAPI perf_rs_program4fv(GLuint program, GLint location, GLsizei count, const float *value)
{
    typedef void (WINAPI *fn_t)(GLuint,GLint,GLsizei,const float *);
    static fn_t real;
    if (!real) real=(fn_t)trace_resolve("glProgramUniform4fv");
    if (real) real(program,location,count,value);
    perf_rs_uniform4(program,location,count,value);
}
static void WINAPI perf_rs_program4f(GLuint program, GLint location, float x, float y, float z, float w)
{
    typedef void (WINAPI *fn_t)(GLuint,GLint,float,float,float,float);
    static fn_t real;
    if (!real) real=(fn_t)trace_resolve("glProgramUniform4f");
    if (real) real(program,location,x,y,z,w);
    float value[4]={x,y,z,w}; perf_rs_uniform4(program,location,1,value);
}
static void WINAPI perf_rs_uniform1i(GLint location, GLint value)
{
    typedef void (WINAPI *fn_t)(GLint,GLint);
    static fn_t real;
    if (!real) real=(fn_t)trace_resolve("glUniform1i");
    if (real) real(location,value);
    perf_rs_uniform_sampler(g_current_program,location,(GLuint64)value,0);
}
static void WINAPI perf_rs_uniform1iv(GLint location, GLsizei count, const GLint *value)
{
    typedef void (WINAPI *fn_t)(GLint,GLsizei,const GLint *);
    static fn_t real;
    if (!real) real=(fn_t)trace_resolve("glUniform1iv");
    if (real) real(location,count,value);
    if (count==1 && value) perf_rs_uniform_sampler(g_current_program,location,(GLuint64)*value,0);
}
static void WINAPI perf_rs_program1i(GLuint program, GLint location, GLint value)
{
    typedef void (WINAPI *fn_t)(GLuint,GLint,GLint);
    static fn_t real;
    if (!real) real=(fn_t)trace_resolve("glProgramUniform1i");
    if (real) real(program,location,value);
    perf_rs_uniform_sampler(program,location,(GLuint64)value,0);
}
static void WINAPI perf_rs_program1iv(GLuint program, GLint location, GLsizei count, const GLint *value)
{
    typedef void (WINAPI *fn_t)(GLuint,GLint,GLsizei,const GLint *);
    static fn_t real;
    if (!real) real=(fn_t)trace_resolve("glProgramUniform1iv");
    if (real) real(program,location,count,value);
    if (count==1 && value) perf_rs_uniform_sampler(program,location,(GLuint64)*value,0);
}
static void WINAPI perf_rs_delete_program(GLuint program)
{
    typedef void (WINAPI *fn_t)(GLuint);
    static fn_t real;
    if (!real) real=(fn_t)trace_resolve("glDeleteProgram");
    perf_rs_forget(program);
    if (real) real(program);
}
static void WINAPI perf_rs_block_binding(GLuint program, GLuint index, GLuint binding)
{
    typedef void (WINAPI *fn_t)(GLuint,GLuint,GLuint);
    static fn_t real;
    if (!real) real=(fn_t)trace_resolve("glUniformBlockBinding");
    if (program<65536 && g_perf_rs_programs[program] &&
        g_perf_rs_programs[program]->program &&
        (index>=8 || g_perf_rs_programs[program]->blocks[index]!=(GLint)binding))
        perf_rs_forget(program);
    if (real) real(program,index,binding);
}
static void WINAPI perf_rs_storage_binding(GLuint program, GLuint index, GLuint binding)
{
    typedef void (WINAPI *fn_t)(GLuint,GLuint,GLuint);
    static fn_t real;
    if (!real) real=(fn_t)trace_resolve("glShaderStorageBlockBinding");
    if (program<65536 && g_perf_rs_programs[program] && g_perf_rs_programs[program]->program)
        perf_rs_forget(program);
    if (real) real(program,index,binding);
}
static void WINAPI perf_rs_program_binary(GLuint program, GLenum format, const void *binary, GLsizei length)
{
    typedef void (WINAPI *fn_t)(GLuint,GLenum,const void *,GLsizei);
    static fn_t real;
    if (!real) real=(fn_t)trace_resolve("glProgramBinary");
    perf_rs_forget(program);
    if (real) real(program,format,binary,length);
}
static void perf_rs_release_handle_slot(unsigned i)
{
    if (i >= g_bindless_handle_count) return;
    memset(&g_bindless_handles[i], 0, sizeof g_bindless_handles[i]);
    memset(&g_perf_rs_handle_targets[i], 0, sizeof g_perf_rs_handle_targets[i]);
    while (g_bindless_handle_count &&
           !g_bindless_handles[g_bindless_handle_count - 1].handle)
        g_bindless_handle_count--;
}

void WINAPI perf_rs_delete_textures(GLsizei count, const GLuint *textures)
{
    if (count>0) g_perf_rs_units_known=0;
    typedef void (WINAPI *fn_t)(GLsizei,const GLuint *);
    static fn_t real;
    if (!real) real=(fn_t)perf_rs_proc("glDeleteTextures");
    bindless_sampler_handle_cache_forget_texture(count, textures);
    if (count>0 && textures) for (unsigned i=0;i<g_bindless_handle_count;i++)
        for (GLsizei j=0;j<count;j++) if (g_bindless_handles[i].texture==textures[j]) {
            perf_rs_release_handle_slot(i);
            break;
        }
    if (real) real(count,textures);
}
static void WINAPI perf_rs_delete_samplers(GLsizei count, const GLuint *samplers)
{
    if (count>0) g_perf_rs_units_known=0;
    typedef void (WINAPI *fn_t)(GLsizei,const GLuint *);
    static fn_t real;
    if (!real) real=(fn_t)trace_resolve("glDeleteSamplers");
    bindless_sampler_handle_cache_forget_sampler(count, samplers);
    if (count>0 && samplers) for (unsigned i=0;i<g_bindless_handle_count;i++)
        for (GLsizei j=0;j<count;j++) if (samplers[j] && g_bindless_handles[i].sampler==samplers[j]) {
            perf_rs_release_handle_slot(i);
            break;
        }
    if (real) real(count,samplers);
}
static void WINAPI perf_rs_bind_buffers_base(GLenum target, GLuint first, GLsizei count, const GLuint *buffers)
{
    static void (WINAPI *real)(GLenum,GLuint,GLsizei,const GLuint *);
    if (!real) real=(__typeof__(real))trace_resolve("glBindBuffersBase");
    for (GLsizei i=0;i<count;i++) {
        GLuint index=first+(GLuint)i;
        GLuint buffer=buffers ? buffers[i] : 0;
        perf_rs_upload_gpu_target(target,buffer);
        if (target==0x8A11 && index<64) {
            GLsizeiptr size=buffer<(1u<<20) ? g_buffer_size[buffer] : 0;
            trace_core_ubo_bind(index,buffer,0,size);
            trace_ubo_bind(index,buffer,0,size);
        }
    }
    if (real) real(target,first,count,buffers);
}
static void WINAPI perf_rs_bind_buffers_range(GLenum target, GLuint first, GLsizei count,
    const GLuint *buffers, const GLintptr *offsets, const GLsizeiptr *sizes)
{
    static void (WINAPI *real)(GLenum,GLuint,GLsizei,const GLuint *,const GLintptr *,const GLsizeiptr *);
    if (!real) real=(__typeof__(real))trace_resolve("glBindBuffersRange");
    for (GLsizei i=0;i<count;i++) {
        GLuint index=first+(GLuint)i;
        GLuint buffer=buffers ? buffers[i] : 0;
        perf_rs_upload_gpu_target(target,buffer);
        if (target==0x8A11 && index<64) {
            GLintptr offset=buffer && offsets ? offsets[i] : 0;
            GLsizeiptr size=buffer && sizes ? sizes[i] : 0;
            trace_core_ubo_bind(index,buffer,offset,size);
            trace_ubo_bind(index,buffer,offset,size);
        }
    }
    if (real) real(target,first,count,buffers,offsets,sizes);
}
static void WINAPI perf_rs_feedback_buffer_base(GLuint feedback, GLuint index, GLuint buffer)
{
    static void (WINAPI *real)(GLuint,GLuint,GLuint);
    if (!real) real=(__typeof__(real))trace_resolve("glTransformFeedbackBufferBase");
    perf_rs_upload_gpu_target(0x8C8E,buffer);
    if (real) real(feedback,index,buffer);
}
static void WINAPI perf_rs_feedback_buffer_range(GLuint feedback, GLuint index, GLuint buffer,
    GLintptr offset, GLsizeiptr size)
{
    static void (WINAPI *real)(GLuint,GLuint,GLuint,GLintptr,GLsizeiptr);
    if (!real) real=(__typeof__(real))trace_resolve("glTransformFeedbackBufferRange");
    perf_rs_upload_gpu_target(0x8C8E,buffer);
    if (real) real(feedback,index,buffer,offset,size);
}
static void WINAPI perf_rs_tex_buffer(GLenum target, GLenum format, GLuint buffer)
{
    static void (WINAPI *real)(GLenum,GLenum,GLuint);
    if (!real) real=(__typeof__(real))trace_resolve("glTexBuffer");
    perf_rs_upload_gpu_target(0x8C2A,buffer);
    if (real) real(target,format,buffer);
}
static void WINAPI perf_rs_tex_buffer_range(GLenum target, GLenum format, GLuint buffer,
    GLintptr offset, GLsizeiptr size)
{
    static void (WINAPI *real)(GLenum,GLenum,GLuint,GLintptr,GLsizeiptr);
    if (!real) real=(__typeof__(real))trace_resolve("glTexBufferRange");
    perf_rs_upload_gpu_target(0x8C2A,buffer);
    if (real) real(target,format,buffer,offset,size);
}
static void WINAPI perf_rs_bind_sampler(GLuint unit, GLuint sampler)
{
    static void (WINAPI *real)(GLuint,GLuint);
    if (!real) real=(__typeof__(real))trace_resolve("glBindSampler");
    perf_rs_invalidate_units(unit,1);
    if (real) real(unit,sampler);
}

static void WINAPI perf_rs_bind_samplers(GLuint first, GLsizei count, const GLuint *samplers)
{
    static void (WINAPI *real)(GLuint,GLsizei,const GLuint *);
    if (!real) real=(__typeof__(real))trace_resolve("glBindSamplers");
    perf_rs_invalidate_units(first,count);
    if (real) real(first,count,samplers);
}

void WINAPI perf_rs_pop_attrib(void)
{
    static void (WINAPI *real)(void);
    static void (WINAPI *get)(GLenum,GLint *);
    if (!real) real=(__typeof__(real))perf_rs_proc("glPopAttrib");
    if (!get) get=(__typeof__(get))perf_rs_proc("glGetIntegerv");
    g_perf_rs_units_known=0;
    if (real) real();
    /* GL_TEXTURE_BIT also restores the active texture selector. */
    if (get) { GLint active=0; get(0x84E0,&active); g_active_texture_unit=(GLenum)active; }
}

static PROC perf_rs_wrapper(const char *name)
{
#define RS_WRAP(api, function) if (!strcmp(name,api)) return (PROC)function
    RS_WRAP("glBindSampler",perf_rs_bind_sampler);
    RS_WRAP("glBindSamplers",perf_rs_bind_samplers);
    RS_WRAP("glPopAttrib",perf_rs_pop_attrib);
    RS_WRAP("glActiveTextureARB",wrap_glActiveTexture);
    RS_WRAP("glBindTextureEXT",wrap_glBindTexture);
    RS_WRAP("glBindBuffersBase",perf_rs_bind_buffers_base);
    RS_WRAP("glBindBuffersRange",perf_rs_bind_buffers_range);
    RS_WRAP("glTransformFeedbackBufferBase",perf_rs_feedback_buffer_base);
    RS_WRAP("glTransformFeedbackBufferRange",perf_rs_feedback_buffer_range);
    RS_WRAP("glTexBuffer",perf_rs_tex_buffer);
    RS_WRAP("glTexBufferARB",perf_rs_tex_buffer);
    RS_WRAP("glTexBufferEXT",perf_rs_tex_buffer);
    RS_WRAP("glTexBufferRange",perf_rs_tex_buffer_range);
    RS_WRAP("glProgramUniform4fv",perf_rs_program4fv);
    RS_WRAP("glProgramUniform4fvEXT",perf_rs_program4fv);
    RS_WRAP("glProgramUniform4f",perf_rs_program4f);
    RS_WRAP("glProgramUniform4fEXT",perf_rs_program4f);
    RS_WRAP("glUniform4fvARB",wrap_glUniform4fv);
    RS_WRAP("glUniform4fARB",wrap_glUniform4f);
    RS_WRAP("glUniform1i",perf_rs_uniform1i);
    RS_WRAP("glUniform1iARB",perf_rs_uniform1i);
    RS_WRAP("glUniform1iv",perf_rs_uniform1iv);
    RS_WRAP("glUniform1ivARB",perf_rs_uniform1iv);
    RS_WRAP("glProgramUniform1i",perf_rs_program1i);
    RS_WRAP("glProgramUniform1iEXT",perf_rs_program1i);
    RS_WRAP("glProgramUniform1iv",perf_rs_program1iv);
    RS_WRAP("glProgramUniform1ivEXT",perf_rs_program1iv);
    RS_WRAP("glDeleteProgram",perf_rs_delete_program);
    RS_WRAP("glUniformBlockBinding",perf_rs_block_binding);
    RS_WRAP("glShaderStorageBlockBinding",perf_rs_storage_binding);
    RS_WRAP("glProgramBinary",perf_rs_program_binary);
    RS_WRAP("glDeleteTextures",perf_rs_delete_textures);
    RS_WRAP("glDeleteSamplers",perf_rs_delete_samplers);
#undef RS_WRAP
    return NULL;
}
