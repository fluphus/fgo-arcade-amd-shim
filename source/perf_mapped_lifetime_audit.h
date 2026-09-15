/* Diagnostic only: compare existing fresh reads, never supply cached bytes. */
#ifdef FGO_MAPPED_LIFETIME_AUDIT
#define PERF_MAPPED_AUDIT_SLOTS 1024
#define PERF_MAPPED_AUDIT_BYTES 256
static volatile uint64_t g_perf_mapped_audit_reads;
static volatile uint64_t g_perf_mapped_audit_eligible;
static volatile uint64_t g_perf_mapped_audit_hits;
static volatile uint64_t g_perf_mapped_audit_changed;
static volatile uint64_t g_perf_mapped_audit_reusable_bytes;
static volatile uint64_t g_perf_mapped_audit_flushes;
static volatile uint64_t g_perf_mapped_audit_boundaries;
static volatile uint64_t g_perf_mapped_audit_unflushed_hits;
static volatile uint64_t g_perf_mapped_audit_collisions;
static uint64_t g_perf_mapped_audit_epoch = 1;
static struct {
    const unsigned char *map;
    GLuint buffer;
    uint32_t serial;
    GLintptr begin, end;
} g_perf_mapped_audit_flushed[BINDLESS_MAP_MAX];
static struct {
    const unsigned char *source, *map;
    size_t size;
    GLuint buffer;
    uint32_t serial;
    uint64_t frame, epoch;
    unsigned char data[PERF_MAPPED_AUDIT_BYTES];
} g_perf_mapped_audit_cache[PERF_MAPPED_AUDIT_SLOTS];
/* Append-only examples, published by sequence after the fields are complete. */
static struct {
    volatile LONG64 sequence;
    uint64_t frame, program, buffer, offset, size, access, gpu_exposed;
    uint64_t serial, first_diff, old_byte, new_byte;
} g_perf_mapped_audit_mismatches[32];

static void perf_mapped_audit_boundary(void)
{
    g_perf_mapped_audit_epoch++;
    g_perf_mapped_audit_boundaries++;
}

static void perf_mapped_audit_flush(bindless_map_rec *m, GLintptr offset,
                                  GLsizeiptr length)
{
    g_perf_mapped_audit_flushes++;
    perf_mapped_audit_boundary();
    if (!m || !m->ptr || m->buffer >= (1u << 20) || offset < 0 ||
        length <= 0 || offset > m->length || length > m->length-offset) return;
    unsigned slot = m->buffer % BINDLESS_MAP_MAX;
    g_perf_mapped_audit_flushed[slot].map = m->ptr;
    g_perf_mapped_audit_flushed[slot].buffer = m->buffer;
    g_perf_mapped_audit_flushed[slot].serial = g_buffer_write_serial[m->buffer];
    g_perf_mapped_audit_flushed[slot].begin = offset;
    g_perf_mapped_audit_flushed[slot].end = offset + length;
}

static void perf_mapped_audit_note(bindless_map_rec *m,
                                 const unsigned char *source, size_t size,
                                 const unsigned char *fresh)
{
    if (!m) return;
    g_perf_mapped_audit_reads++;
    if (m->access != 0x52u || !size || size > PERF_MAPPED_AUDIT_BYTES ||
        m->buffer >= (1u << 20)) return;
    g_perf_mapped_audit_eligible++;
    uint32_t serial = g_buffer_write_serial[m->buffer];
    uintptr_t h = (uintptr_t)source;
    unsigned slot = (unsigned)((h >> 4) ^ (h >> 17) ^ (size * 31)) &
                    (PERF_MAPPED_AUDIT_SLOTS - 1);
    __typeof__(g_perf_mapped_audit_cache[0]) *r = &g_perf_mapped_audit_cache[slot];
    int live = r->epoch == g_perf_mapped_audit_epoch && r->frame == g_frame_count;
    if (live && r->source == source && r->map == m->ptr && r->size == size &&
        r->buffer == m->buffer && r->serial == serial) {
        g_perf_mapped_audit_hits++;
        unsigned f = m->buffer % BINDLESS_MAP_MAX;
        GLintptr offset = (GLintptr)(source - m->ptr);
        if (g_perf_mapped_audit_flushed[f].map != m->ptr ||
            g_perf_mapped_audit_flushed[f].buffer != m->buffer ||
            g_perf_mapped_audit_flushed[f].serial != serial ||
            offset < g_perf_mapped_audit_flushed[f].begin ||
            offset + (GLintptr)size > g_perf_mapped_audit_flushed[f].end)
            g_perf_mapped_audit_unflushed_hits++;
        if (memcmp(r->data, fresh, size)) {
            uint64_t n = g_perf_mapped_audit_changed++;
            if (n < 32) {
                __typeof__(g_perf_mapped_audit_mismatches[0]) *e =
                    &g_perf_mapped_audit_mismatches[n];
                size_t first = 0;
                while (first < size && r->data[first] == fresh[first]) first++;
                e->frame=g_frame_count; e->program=g_current_program;
                e->buffer=m->buffer; e->offset=m->offset+offset;
                e->size=size; e->access=m->access; e->serial=serial;
                e->gpu_exposed=!!(g_perf_rs_gpu_buffers[m->buffer/8] &
                                    (1u << (m->buffer & 7)));
                e->first_diff=first; e->old_byte=r->data[first];
                e->new_byte=fresh[first];
                InterlockedExchange64(&e->sequence, 2);
            }
        } else {
            g_perf_mapped_audit_reusable_bytes += size;
        }
    } else if (live) {
        g_perf_mapped_audit_collisions++;
    }
    r->source=source; r->map=m->ptr; r->size=size; r->buffer=m->buffer;
    r->serial=serial; r->frame=g_frame_count; r->epoch=g_perf_mapped_audit_epoch;
    memcpy(r->data, fresh, size);
}
#endif
