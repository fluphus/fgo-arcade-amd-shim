static unsigned formation_seen, formation_pending;
static U formation_output;

static U formation_attachment(void)
{
    I fbo=0,type=0,texture=0;
    glGetIntegerv(0x8ca6,&fbo);
    if(fbo<=0) return 0;
    glGetNamedFramebufferAttachmentParameteriv(fbo,0x8ce0,0x8cd0,&type);
    if(type!=0x1702) return 0;
    glGetNamedFramebufferAttachmentParameteriv(fbo,0x8ce0,0x8cd1,&texture);
    return (U)texture;
}

static U formation_unit(U unit)
{
    I previous=0,texture=0;
    glGetIntegerv(0x84e0,&previous);
    active_texture(0x84c0+unit);
    glGetIntegerv(0x8069,&texture);
    active_texture((U)previous);
    return (U)texture;
}

static void formation_pixels(U texture,const char *role)
{
    static void (WINAPI *subimage)(U,I,I,I,I,I,I,I,U,U,I,void *);
    if(stopped || !texture || !glIsTexture(texture)) return;
    if(!subimage) subimage=(void *)scene_resolve("glGetTextureSubImage");
    if(!subimage) { issue("formation_subimage_unavailable"); return; }
    I width=0,height=0,internal=0;
    glGetTextureLevelParameteriv(texture,0,0x1000,&width);
    glGetTextureLevelParameteriv(texture,0,0x1001,&height);
    glGetTextureLevelParameteriv(texture,0,0x1003,&internal);
    if(width<448 || height<1080) { issue("formation_texture_shape"); return; }
    uint64_t size=448u*1080u*8u;
    if(!available(size,1)) return;
    void *data=calloc(1,(size_t)size);
    if(!data) { issue("allocation_failed"); stopped=1; return; }
    Pack pack; pack_begin(&pack,1);
    errors("before_formation_read");
    subimage(texture,0,0,0,0,448,1080,1,0x1908,0x140b,(I)size,data);
    gpu_bytes+=size;
    int failed=errors("formation_read");
    pack_end(&pack);
    unsigned blob=0;
    if(failed) free(data); else blob=take_blob(data,size);
    row(); j("{\"event\":\"formation_pixels\",\"seq\":%llu,\"role\":",sequence); js(role);
    j(",\"texture\":%u,\"texture_width\":%d,\"texture_height\":%d,\"internal\":%d,"
      "\"x\":0,\"y\":0,\"width\":448,\"height\":1080,\"format\":6408,\"type\":5131,\"blob\":%u}",
      texture,width,height,internal,blob); end_row(0);
}

static void formation_before(U object)
{
    if(object!=654 && object!=153) return;
    unsigned bit=object==654?1u:2u;
    if(formation_seen&bit) return;
    I viewport[4]={0}; glGetIntegerv(0x0ba2,viewport);
    if(viewport[0] || viewport[1] || viewport[2]!=448 || viewport[3]!=1080) return;
    formation_seen|=bit;
    formation_output=formation_attachment();
    if(object==654) {
        formation_pixels(formation_output,"model_before_alpha_fix");
        formation_pixels(formation_unit(0),"alpha_fix_color_input");
        formation_pixels(formation_unit(1),"alpha_fix_mask_input");
    } else formation_pixels(formation_unit(0),"before_tonemap");
    formation_pending=bit;
}

static void formation_after(void)
{
    if(scene_phase!=2 || stopped || GetCurrentThreadId()!=scene_thread || !formation_pending) return;
    unsigned pending=formation_pending; formation_pending=0;
    formation_pixels(formation_output,pending==1?"model_after_alpha_fix":"after_tonemap");
}
