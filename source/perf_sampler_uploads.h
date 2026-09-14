/* CPU bytes for ordinary samplers only; validity is tracked per uploaded byte. */
#define PERF_RS_UPLOAD_CAP 65536
#define PERF_RS_UPLOAD_MAX (256u * 1024u)
#define PERF_RS_UPLOAD_TOTAL (16u * 1024u * 1024u)
static int g_perf_regular_samplers_on;
static unsigned long long g_perf_rs_upload_hits, g_perf_rs_upload_misses;
static unsigned char g_perf_rs_gpu_buffers[(1u << 20) / 8];
static HGLRC g_perf_rs_upload_context;
static size_t g_perf_rs_upload_bytes;
typedef struct {
    GLuint buffer;
    uint32_t serial;
    size_t size;
    unsigned char *data;
} perf_rs_upload;
static perf_rs_upload g_perf_rs_uploads[PERF_RS_UPLOAD_CAP];

static perf_rs_upload *perf_rs_upload_find(GLuint buffer)
{
    if (!buffer || buffer>=PERF_RS_UPLOAD_CAP || !g_perf_rs_uploads[buffer].buffer) return NULL;
    return &g_perf_rs_uploads[buffer];
}

static void perf_rs_upload_free(perf_rs_upload *r)
{
    if (r->data) {
        g_perf_rs_upload_bytes-=r->size+(r->size+7)/8;
        free(r->data);
    }
    memset(r,0,sizeof *r);
}

static void perf_rs_upload_bits(perf_rs_upload *r, size_t offset, size_t size, int valid)
{
    unsigned char *bits=r->data+r->size;
    size_t end=offset+size;
    while (offset<end && (offset&7)) {
        unsigned char mask=(unsigned char)(1u<<(offset&7));
        if (valid) bits[offset/8]|=mask; else bits[offset/8]&=(unsigned char)~mask;
        offset++;
    }
    size_t whole=(end-offset)/8;
    memset(bits+offset/8,valid?255:0,whole); offset+=whole*8;
    while (offset<end) {
        unsigned char mask=(unsigned char)(1u<<(offset&7));
        if (valid) bits[offset/8]|=mask; else bits[offset/8]&=(unsigned char)~mask;
        offset++;
    }
}

static void perf_rs_upload_invalidate(GLuint buffer, GLintptr offset, GLsizeiptr size)
{
    if (!g_perf_regular_samplers_on || !buffer || buffer>=(1u<<20)) return;
    perf_rs_upload *r=perf_rs_upload_find(buffer);
    if (!r) return;
    if (size<0 || offset<0) { perf_rs_upload_free(r); return; }
    uint32_t serial=g_buffer_write_serial[buffer];
    if (r->serial!=serial && (uint32_t)(r->serial+1)!=serial)
        memset(r->data+r->size,0,(r->size+7)/8);
    if ((size_t)offset<r->size && size>0) {
        size_t n=(size_t)size;
        if (n>r->size-(size_t)offset) n=r->size-(size_t)offset;
        perf_rs_upload_bits(r,(size_t)offset,n,0);
    }
}

static void perf_rs_upload_gpu_target(GLenum target, GLuint buffer)
{
    if (!g_perf_regular_samplers_on || !buffer || buffer>=(1u<<20)) return;
    if (target!=0x90D2 && target!=0x8C8E && target!=0x92C0 &&
        target!=0x88EB) return;
    /* A buffer exposed to shader/feedback/pack writes stays ineligible even
       after unbinding. This also covers writes without a new API upload. */
    g_perf_rs_gpu_buffers[buffer/8]|=(unsigned char)(1u<<(buffer&7));
    perf_rs_upload_invalidate(buffer,0,-1);
    perf_mapped_flush_shadow_gpu_exposed(buffer);
}

static void perf_rs_upload_note(GLuint buffer, GLintptr offset, GLsizeiptr size,
                                const void *data)
{
    if (!g_perf_regular_samplers_on || !buffer || buffer>=PERF_RS_UPLOAD_CAP ||
        !data || offset<0 || size<=0 || bindless_map_find(buffer) ||
        (g_perf_rs_gpu_buffers[buffer/8]&(1u<<(buffer&7)))) return;
    GLsizeiptr backing=g_buffer_size[buffer];
    if (backing<=0 || backing>PERF_RS_UPLOAD_MAX || offset>backing || size>backing-offset) return;
    perf_rs_upload *r=perf_rs_upload_find(buffer);
    if (r && r->size!=(size_t)backing) { perf_rs_upload_free(r); r=NULL; }
    if (!r) {
        /* Static material uploads cannot be reconstructed after eviction.
           Each supported buffer keeps its own slot until invalidation. */
        r=&g_perf_rs_uploads[buffer];
        size_t allocation=(size_t)backing+((size_t)backing+7)/8;
        if (allocation>PERF_RS_UPLOAD_TOTAL-g_perf_rs_upload_bytes) return;
        r->data=malloc(allocation);
        if (!r->data) return;
        r->buffer=buffer; r->size=(size_t)backing;
        memset(r->data+r->size,0,(r->size+7)/8);
        g_perf_rs_upload_bytes+=allocation;
    }
    memcpy(r->data+(size_t)offset,data,(size_t)size);
    perf_rs_upload_bits(r,(size_t)offset,(size_t)size,1);
    r->serial=g_buffer_write_serial[buffer];
}

static int perf_rs_upload_copy_valid(GLuint buffer, GLintptr offset, GLsizeiptr size, void *data)
{
    if (!g_perf_regular_samplers_on || !buffer || buffer>=(1u<<20) ||
        offset<0 || size<=0 || !data) return 0;
    perf_rs_upload *r=perf_rs_upload_find(buffer);
    if (!r || bindless_map_find(buffer) || r->serial!=g_buffer_write_serial[buffer] ||
        (g_perf_rs_gpu_buffers[buffer/8]&(1u<<(buffer&7))) ||
        (size_t)offset>r->size || (size_t)size>r->size-(size_t)offset) goto miss;
    unsigned char *bits=r->data+r->size;
    size_t at=(size_t)offset, end=at+(size_t)size;
    while (at<end && (at&7)) { if (!(bits[at/8]&(1u<<(at&7)))) goto miss; at++; }
    while (end-at>=8) { if (bits[at/8]!=255) goto miss; at+=8; }
    while (at<end) { if (!(bits[at/8]&(1u<<(at&7)))) goto miss; at++; }
    memcpy(data,r->data+(size_t)offset,(size_t)size); return 1;
miss:
    return 0;
}

static int perf_rs_upload_copy(GLuint buffer, GLintptr offset, GLsizeiptr size, void *data)
{
    if (!g_perf_regular_samplers_on || !buffer || buffer>=(1u<<20) ||
        offset<0 || size<=0 || !data) return 0;
    int copied=perf_rs_upload_copy_valid(buffer,offset,size,data);
    if (copied) g_perf_rs_upload_hits++; else g_perf_rs_upload_misses++;
    return copied;
}

static void perf_rs_upload_context_change(HGLRC context)
{
    if (g_perf_rs_upload_context==context) return;
    for (unsigned i=0;i<PERF_RS_UPLOAD_CAP;i++) perf_rs_upload_free(&g_perf_rs_uploads[i]);
    memset(g_perf_rs_gpu_buffers,0,sizeof g_perf_rs_gpu_buffers);
    g_perf_rs_upload_context=context;
    perf_mapped_flush_shadow_context_reset();
}
