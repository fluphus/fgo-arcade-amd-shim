/* Small sampler fields in explicitly flushed persistent mappings. The
   application publishes CPU writes with flushes and GPU writes with barriers
   or waits. Those calls, buffer mutations and context changes advance epoch.
   Frames also form a hard boundary. Mapping flags are never changed. */
#define PERF_MAPPED_READ_SLOTS 1024
#define PERF_MAPPED_READ_BYTES 256
static int g_perf_mapped_read_cache_on = 1;
static uint64_t g_perf_mapped_read_epoch = 1;
static unsigned long long g_perf_mapped_read_cache_hits;
static unsigned long long g_perf_mapped_read_cache_misses;
static unsigned long long g_perf_mapped_read_cache_bytes;
typedef struct {
    const unsigned char *source, *map;
    size_t size;
    GLuint buffer;
    uint32_t serial;
    uint64_t frame, epoch;
    unsigned char data[PERF_MAPPED_READ_BYTES];
} perf_mapped_read_entry;
static perf_mapped_read_entry g_perf_mapped_read_cache[PERF_MAPPED_READ_SLOTS];

static void perf_mapped_read_boundary(void)
{
    g_perf_mapped_read_epoch++;
#ifdef FGO_MAPPED_LIFETIME_AUDIT
    perf_mapped_audit_boundary();
#endif
}

static void perf_mapped_read_flush(bindless_map_rec *m, GLintptr offset,
                                  GLsizeiptr length)
{
    g_perf_mapped_read_epoch++;
#ifdef FGO_MAPPED_LIFETIME_AUDIT
    perf_mapped_audit_flush(m,offset,length);
#endif
}

static int perf_mapped_read_eligible(bindless_map_rec *m, size_t size)
{
#ifdef FGO_MAPPED_LIFETIME_AUDIT
    /* The independent diagnostic must continue comparing fresh bytes. */
    return 0;
#else
    return g_perf_mapped_read_cache_on && m && m->active && m->access==0x52u &&
        m->buffer>0 && m->buffer<(1u<<20) && size>0 && size<=PERF_MAPPED_READ_BYTES;
#endif
}

static perf_mapped_read_entry *perf_mapped_read_slot(const unsigned char *source,
                                                   size_t size)
{
    uintptr_t h=(uintptr_t)source;
    return &g_perf_mapped_read_cache[((h>>4)^(h>>17)^(size*31)) &
                                   (PERF_MAPPED_READ_SLOTS-1)];
}

static int perf_mapped_read_copy(bindless_map_rec *m, const unsigned char *source,
                                 size_t size, void *dst)
{
    if (!perf_mapped_read_eligible(m,size)) return 0;
    perf_mapped_read_entry *r=perf_mapped_read_slot(source,size);
    if (r->epoch==g_perf_mapped_read_epoch && r->frame==g_frame_count &&
        r->source==source && r->map==m->ptr && r->size==size &&
        r->buffer==m->buffer && r->serial==g_buffer_write_serial[m->buffer]) {
        memcpy(dst,r->data,size);
        g_perf_mapped_read_cache_hits++;
        g_perf_mapped_read_cache_bytes+=size;
        return 1;
    }
    g_perf_mapped_read_cache_misses++;
    return 0;
}

static void perf_mapped_read_store(bindless_map_rec *m, const unsigned char *source,
                                   size_t size, const void *fresh)
{
    if (!perf_mapped_read_eligible(m,size)) return;
    perf_mapped_read_entry *r=perf_mapped_read_slot(source,size);
    r->source=source; r->map=m->ptr; r->size=size; r->buffer=m->buffer;
    r->serial=g_buffer_write_serial[m->buffer];
    r->frame=g_frame_count; r->epoch=g_perf_mapped_read_epoch;
    memcpy(r->data,fresh,size);
}
