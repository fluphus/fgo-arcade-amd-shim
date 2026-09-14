/* Diagnostic object IDs are verified against shader signatures in this process. */
static unsigned refraction_seen, refraction_pending;
static U refraction_output;

static int refraction_program_matches(U object, const char *needle)
{
    U shaders[8]; I count=0;
    glGetAttachedShaders(object,8,&count,shaders);
    for(I i=0;i<count;i++) {
        I type=0,size=0;
        glGetShaderiv(shaders[i],0x8b4f,&type);
        if(type!=0x8b30) continue;
        glGetShaderiv(shaders[i],0x8b88,&size);
        if(size<=0 || size>1024*1024) continue;
        char *source=calloc((size_t)size+1,1);
        if(!source) return 0;
        glGetShaderSource(shaders[i],size,NULL,source);
        int match=strstr(source,needle)!=NULL;
        free(source);
        if(match) return 1;
    }
    return 0;
}

static U refraction_attachment(U attachment)
{
    I fbo=0,type=0,texture=0;
    glGetIntegerv(0x8ca6,&fbo);
    if(fbo<=0) return 0;
    glGetNamedFramebufferAttachmentParameteriv(fbo,attachment,0x8cd0,&type);
    if(type!=0x1702) return 0;
    glGetNamedFramebufferAttachmentParameteriv(fbo,attachment,0x8cd1,&texture);
    return (U)texture;
}

static U refraction_unit(U unit,U *sampler)
{
    I previous=0,texture=0,bound_sampler=0;
    glGetIntegerv(0x84e0,&previous);
    active_texture(0x84c0+unit);
    glGetIntegerv(0x8069,&texture);
    glGetIntegeri_v(0x8919,unit,&bound_sampler);
    active_texture((U)previous);
    *sampler=(U)bound_sampler;
    return (U)texture;
}

static void refraction_texture_state(U texture,U sampler,const char *role)
{
    I swizzle[4]={0};
    glGetTextureParameteriv(texture,0x8e46,swizzle);
    row(); j("{\"event\":\"refraction_texture_state\",\"seq\":%llu,\"texture\":%u,\"sampler\":%u,\"role\":",sequence,texture,sampler);
    js(role); j(",\"swizzle\":[%d,%d,%d,%d],\"texture_parameters\":[",swizzle[0],swizzle[1],swizzle[2],swizzle[3]);
    const U tex_names[]={0x813c,0x813d,0x82df,0x90ea};
    for(unsigned i=0;i<sizeof tex_names/sizeof tex_names[0];i++) {
        I value=0; glGetTextureParameteriv(texture,tex_names[i],&value);
        j("%s[%u,%d]",i?",":"",tex_names[i],value);
    }
    j("],\"sampling_parameters\":[");
    const U names[]={0x2800,0x2801,0x2802,0x2803,0x884c,0x884d};
    for(unsigned i=0;i<sizeof names/sizeof names[0];i++) {
        I value=0;
        if(sampler) glGetSamplerParameteriv(sampler,names[i],&value);
        else glGetTextureParameteriv(texture,names[i],&value);
        j("%s[%u,%d]",i?",":"",names[i],value);
    }
    j("],\"float_parameters\":[");
    const U floats[]={0x813a,0x813b,0x8501,0x1004};
    for(unsigned i=0;i<sizeof floats/sizeof floats[0];i++) {
        float value[4]={0}; U bits[4];
        if(sampler) glGetSamplerParameterfv(sampler,floats[i],value);
        else glGetTextureParameterfv(texture,floats[i],value);
        memcpy(bits,value,sizeof bits);
        j("%s[%u,%u,%u,%u,%u]",i?",":"",floats[i],bits[0],bits[1],bits[2],bits[3]);
    }
    j("]}"); end_row(0);
}

static void refraction_pixels(U texture,U sampler,const char *role,I level,
                              U format,U type,unsigned pixel_bytes,int asset)
{
    if(stopped || !texture || !glIsTexture(texture)) return;
    I width=0,height=0,depth=0,internal=0,compressed=0;
    glGetTextureLevelParameteriv(texture,level,0x1000,&width);
    glGetTextureLevelParameteriv(texture,level,0x1001,&height);
    glGetTextureLevelParameteriv(texture,level,0x8071,&depth);
    glGetTextureLevelParameteriv(texture,level,0x1003,&internal);
    glGetTextureLevelParameteriv(texture,level,0x86a1,&compressed);
    if(width<=0 || height<=0 || depth!=1) { issue("refraction_texture_shape"); return; }
    uint64_t size=(uint64_t)width*(uint64_t)height*pixel_bytes;
    if(asset && compressed) {
        I bytes=0; glGetTextureLevelParameteriv(texture,level,0x86a0,&bytes);
        if(bytes<=0) { issue("refraction_compressed_size"); return; }
        size=(uint64_t)bytes; format=type=0;
    }
    if(size>INT_MAX || (asset && size>4u*1024u*1024u)) {
        issue("refraction_asset_size_limit"); return;
    }
    if(!available(size,1)) return;
    void *data=calloc(1,(size_t)size);
    if(!data) { issue("allocation_failed"); stopped=1; return; }
    Pack pack; pack_begin(&pack,1);
    errors("before_refraction_texture_read");
    if(asset && compressed) glGetCompressedTextureImage(texture,level,(I)size,data);
    else glGetTextureImage(texture,level,format,type,(I)size,data);
    gpu_bytes+=size;
    int failed=errors("refraction_texture_read");
    pack_end(&pack);
    unsigned id=0;
    if(failed) free(data); else id=take_blob(data,size);
    row(); j("{\"event\":\"refraction_pixels\",\"seq\":%llu,\"role\":",sequence);
    js(role); j(",\"texture\":%u,\"sampler\":%u,\"level\":%d,\"width\":%d,\"height\":%d,\"internal\":%d,\"compressed\":%d,\"format\":%u,\"type\":%u,\"bytes\":%llu,\"blob\":%u}",
        texture,sampler,level,width,height,internal,asset?compressed:0,format,type,size,id); end_row(0);
}

static void refraction_sample_unit(U unit,const char *role,int asset,U format,U type,unsigned pixel_bytes)
{
    U sampler=0,texture=refraction_unit(unit,&sampler);
    if(!texture || !glIsTexture(texture)) { issue("refraction_missing_texture"); return; }
    refraction_texture_state(texture,sampler,role);
    I base=0,maximum=0,levels=0;
    glGetTextureParameteriv(texture,0x813c,&base);
    glGetTextureParameteriv(texture,0x813d,&maximum);
    glGetTextureParameteriv(texture,0x82df,&levels);
    I last=asset && levels>0 ? levels-1 : base;
    if(last>maximum) last=maximum;
    if(base<0 || base>31 || last>31 || last<base) { issue("refraction_mip_range"); return; }
    for(I level=base;level<=last && !stopped;level++)
        refraction_pixels(texture,sampler,role,level,format,type,pixel_bytes,asset);
}

static void refraction_before(U object)
{
    if(object==2172 && !(refraction_seen&1)) {
        refraction_seen|=1;
        if(!refraction_program_matches(object,"#define OUTPUT_ONLY_VELOCITY 1")) {
            issue("refraction_program_signature"); return;
        }
        I mask[4]={0}; glGetIntegerv(0x0c23,mask);
        row(); j("{\"event\":\"refraction_color_mask\",\"seq\":%llu,\"rgba\":[%d,%d,%d,%d]}",sequence,mask[0],mask[1],mask[2],mask[3]); end_row(0);
        refraction_output=refraction_attachment(0x8ce0);
        refraction_texture_state(refraction_output,0,"velocity_target");
        refraction_pixels(refraction_output,0,"velocity_before_particle",0,0x8227,0x140b,4,0);
        refraction_pixels(refraction_attachment(0x8d00),0,"particle_depth_target",0,0x1902,0x1406,4,0);
        refraction_sample_unit(0,"sword_mask",1,0x1908,0x1401,4);
        refraction_sample_unit(2,"sword_normal",1,0x1908,0x1401,4);
        refraction_sample_unit(4,"particle_linear_depth",0,0x1903,0x1406,4);
        refraction_sample_unit(5,"particle_raw_depth",0,0x1902,0x1406,4);
        refraction_pending=1;
    } else if(object==2154 && !(refraction_seen&2)) {
        refraction_seen|=2;
        if(!refraction_program_matches(object,"g_scene_color_sampler")) {
            issue("refraction_program_signature"); return;
        }
        refraction_sample_unit(3,"color_before_direct_particles",0,0x1907,0x140b,6);
        refraction_sample_unit(0,"first_direct_mask",1,0x1908,0x1401,4);
        refraction_sample_unit(2,"first_direct_normal",1,0x1908,0x1401,4);
    } else if(object==562 && !(refraction_seen&4)) {
        refraction_seen|=4;
        if(!refraction_program_matches(object,"(t + velocity) * g_color_texcoord_scale")) {
            issue("refraction_program_signature"); return;
        }
        refraction_sample_unit(1,"velocity_before_composite",0,0x8227,0x140b,4);
        refraction_sample_unit(0,"color_before_composite",0,0x1907,0x140b,6);
        refraction_output=refraction_attachment(0x8ce0);
        refraction_texture_state(refraction_output,0,"composite_target");
        refraction_pending=2;
    }
}

static void refraction_after(U object)
{
    if(scene_phase!=2 || stopped || GetCurrentThreadId()!=scene_thread) return;
    if(object==2172 && refraction_pending==1) {
        refraction_pending=0;
        refraction_pixels(refraction_output,0,"velocity_after_particle",0,0x8227,0x140b,4,0);
    } else if(object==562 && refraction_pending==2) {
        refraction_pending=0;
        refraction_pixels(refraction_output,0,"color_after_composite",0,0x1907,0x140b,6,0);
    }
}
