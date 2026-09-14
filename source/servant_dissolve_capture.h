#include <math.h>

static I dissolve_locations[65536];
static B dissolve_checked[65536];
static U dissolve_program, dissolve_flags;
static I dissolve_location;
static float dissolve_threshold;
static uint64_t dissolve_tick, dissolve_frame;
static int dissolve_ready;
static U dissolve_noise_textures[16];
static unsigned dissolve_noise_count;

/* Uploads of an array may start before the element containing the flag. */
static int dissolve_upload(I location, I count, const float *value,
                            I target, float result[4])
{
    if(!value || location<0 || target<location || count<=0 ||
       (int64_t)target-location>=count) return 0;
    const float *element=value+(size_t)(target-location)*4;
    U flags; memcpy(&flags,element+3,4);
    if(!(flags&1) || !isfinite(element[2]) || fabsf(element[2])>1000000.f) return 0;
    memcpy(result,element,4*sizeof(float)); return 1;
}

static void WINAPI h_dissolve_uniform4(I location,I count,const float *value)
{
    typedef void (WINAPI *UniformFn)(I,I,const float *);
    DWORD error=GetLastError();
    if(scene_phase==6 && dissolve_ready) {
        U program=mirror_u(A_PROGRAM);
        if(program && program<65536) {
            if(!dissolve_checked[program]) {
                if(!glGetUniformLocation) glGetUniformLocation=(void *)scene_resolve("glGetUniformLocation");
                if(glGetUniformLocation) {
                    dissolve_locations[program]=glGetUniformLocation(program,"g_per_draw[1]");
                    dissolve_checked[program]=1;
                }
            }
            float element[4];
            if(dissolve_checked[program] && dissolve_upload(location,count,value,
                    dissolve_locations[program],element)) {
                dissolve_program=program; dissolve_location=dissolve_locations[program];
                memcpy(&dissolve_flags,element+3,4); dissolve_threshold=element[2];
                dissolve_tick=GetTickCount64(); dissolve_frame=mirror_frame();
                scene_phase=1;
            }
        }
    }
    SetLastError(error);
    ((UniformFn)(uintptr_t)scene_request->expected[S_UNIFORM4])(location,count,value);
}

static void dissolve_sample_noise(Program *program)
{
    I location=glGetUniformLocation(program->object,"g_noise_sampler");
    if(location<0 || dissolve_noise_count>=16 || stopped) return;
    I unit=0,active=0,texture=0,sampler=0;
    glGetUniformiv(program->object,location,&unit);
    if(unit<0 || unit>=192) return;
    glGetIntegerv(0x84e0,&active); active_texture(0x84c0+unit);
    glGetIntegerv(0x806a,&texture); glGetIntegeri_v(0x8919,unit,&sampler);
    active_texture(active);
    if(texture<=0) return;
    for(unsigned i=0;i<dissolve_noise_count;i++) if(dissolve_noise_textures[i]==(U)texture) return;
    dissolve_noise_textures[dissolve_noise_count++]=(U)texture;
    I w=0,h=0,d=0,internal=0;
    glGetTextureLevelParameteriv(texture,0,0x1000,&w);
    glGetTextureLevelParameteriv(texture,0,0x1001,&h);
    glGetTextureLevelParameteriv(texture,0,0x8071,&d);
    glGetTextureLevelParameteriv(texture,0,0x1003,&internal);
    uint64_t bytes=(uint64_t)(w>0?w:0)*(h>0?h:0)*(d>0?d:0)*4;
    unsigned blob=0;
    if(bytes && bytes<=8u*1024u*1024u && available(bytes,1)) {
        void *pixels=malloc((size_t)bytes);
        if(pixels) {
            Pack pack; pack_begin(&pack,1);
            glGetTextureImage(texture,0,0x1908,0x1401,(I)bytes,pixels); gpu_bytes+=bytes;
            pack_end(&pack);
            if(!errors("dissolve_noise_pixels")) blob=take_blob(pixels,bytes);
            else free(pixels);
        }
    }
    row(); j("{\"event\":\"dissolve_noise\",\"seq\":%llu,\"program\":%u,\"unit\":%d,\"texture\":%d,\"sampler\":%d,\"width\":%d,\"height\":%d,\"depth\":%d,\"internal\":%d,\"format\":6408,\"type\":5121,\"blob\":%u}",
        sequence,program->object,unit,texture,sampler,w,h,d,internal,blob); end_row(0);
    row(); j("{\"event\":\"dissolve_noise_state\",\"seq\":%llu,\"texture\":%d,\"sampler\":%d,\"sampling\":[",sequence,texture,sampler);
    const U parameters[]={0x2800,0x2801,0x2802,0x2803,0x8072,0x884c,0x884d};
    for(unsigned i=0;i<sizeof parameters/sizeof parameters[0];i++) {
        I value=0;
        if(sampler) glGetSamplerParameteriv(sampler,parameters[i],&value);
        else glGetTextureParameteriv(texture,parameters[i],&value);
        j("%s[%u,%d]",i?",":"",parameters[i],value);
    }
    j("]}"); end_row(0);
}

__declspec(dllexport) unsigned WINAPI FgoDissolveWatchVersion(void) { return 1; }
__declspec(dllexport) int WINAPI FgoDissolveTriggerTest(I location,I count,
                                                       const float *value,I target)
{ float result[4]; return dissolve_upload(location,count,value,target,result); }
