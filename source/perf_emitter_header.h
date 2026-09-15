/* Validated Unused pointer headers; GPU count/index data is never cached. */
#define PERF_EMITTER_HEADER_BUFFERS 65536
static int g_perf_emitter_header_cache_on;
static unsigned long long g_perf_emitter_header_hits, g_perf_emitter_header_misses;
static unsigned long long g_perf_emitter_header_driver_reads, g_perf_emitter_header_driver_ticks;
static HGLRC g_perf_emitter_header_context;
typedef struct {
    GLintptr offset;
    uint64_t pointers[2];
} perf_emitter_header;
typedef struct {
    perf_emitter_header *entries;
    unsigned count, capacity;
} perf_emitter_header_pool;
static perf_emitter_header_pool g_perf_emitter_headers[PERF_EMITTER_HEADER_BUFFERS];
typedef struct {
    uint64_t upload_frame, upload_offset, upload_size, upload_prefix[2];
    uint64_t invalidate_frame, invalidate_offset, invalidate_size;
} perf_emitter_header_history;
static perf_emitter_header_history g_perf_emitter_header_history[PERF_EMITTER_HEADER_BUFFERS];
typedef struct {
    volatile uint64_t sequence;
    uint64_t buffer, offset, program, backing, mapped, cached_headers;
    uint64_t pointers[2], self_relative, reads, ticks, first_frame, last_frame;
    perf_emitter_header_history history;
} perf_emitter_read_record;
#define PERF_EMITTER_READ_RECORD_CAP 128
static perf_emitter_read_record g_perf_emitter_read_records[PERF_EMITTER_READ_RECORD_CAP];
static unsigned long long g_perf_emitter_read_record_overflow;

static unsigned perf_emitter_header_lower_bound(const perf_emitter_header_pool *pool,
                                                 GLintptr offset)
{
    unsigned first = 0, last = pool->count;
    while (first < last) {
        unsigned mid = first + (last - first) / 2;
        if (pool->entries[mid].offset < offset) first = mid + 1;
        else last = mid;
    }
    return first;
}

static void perf_emitter_header_invalidate(GLuint buffer, GLintptr offset,
                                           GLsizeiptr size)
{
    perf_rs_upload_invalidate(buffer, offset, size);
    if (!g_perf_emitter_header_cache_on || !buffer ||
        buffer >= PERF_EMITTER_HEADER_BUFFERS) return;
    perf_emitter_header_history *history = &g_perf_emitter_header_history[buffer];
    history->invalidate_frame = g_frame_count;
    history->invalidate_offset = (uint64_t)offset;
    history->invalidate_size = (uint64_t)size;
    perf_emitter_header_pool *pool = &g_perf_emitter_headers[buffer];
    if (size < 0) {
        free(pool->entries);
        memset(pool, 0, sizeof *pool);
        return;
    }
    if (size <= 0 || !pool->count) return;
    unsigned first = perf_emitter_header_lower_bound(pool, offset > 15 ? offset - 15 : 0);
    unsigned last = first;
    while (last < pool->count && (offset >= pool->entries[last].offset ||
                                 size > pool->entries[last].offset - offset)) last++;
    if (last > first) {
        memmove(pool->entries + first, pool->entries + last,
                (size_t)(pool->count - last) * sizeof *pool->entries);
        pool->count -= last - first;
    }
}

static int perf_emitter_header_matches(GLuint buffer, GLintptr offset,
                                       const void *data)
{
    uint64_t pointers[2];
    if (!buffer || buffer >= 65536 || offset < 0 ||
        offset > 0xffffffffULL - 20 || g_buffer_size[buffer] < offset + 24)
        return 0;
    memcpy(pointers, data, sizeof pointers);
    return pointers[0] == make_fake_addr(buffer, (GLuint)offset + 16) &&
           pointers[1] == make_fake_addr(buffer, (GLuint)offset + 20);
}

static void perf_emitter_header_store(GLuint buffer, GLintptr offset, const void *data)
{
    perf_emitter_header_pool *pool = &g_perf_emitter_headers[buffer];
    unsigned slot = perf_emitter_header_lower_bound(pool, offset);
    if (slot == pool->count || pool->entries[slot].offset != offset) {
        if (pool->count == pool->capacity) {
            unsigned capacity = pool->capacity ? pool->capacity * 2 : 4;
            perf_emitter_header *entries = realloc(pool->entries,
                (size_t)capacity * sizeof *entries);
            if (!entries) return;
            pool->entries = entries;
            pool->capacity = capacity;
        }
        memmove(pool->entries + slot + 1, pool->entries + slot,
                (size_t)(pool->count - slot) * sizeof *pool->entries);
        pool->count++;
    }
    pool->entries[slot].offset = offset;
    memcpy(pool->entries[slot].pointers, data, 16);
}

static void perf_emitter_header_admit_read(GLuint buffer, GLintptr offset, const void *data)
{
    if (!g_perf_emitter_header_cache_on ||
        !perf_emitter_header_matches(buffer, offset, data) || bindless_map_find(buffer)) return;
    /* The free-list reset shader writes these same addresses on every reset. */
    perf_emitter_header_store(buffer, offset, data);
}

static void perf_emitter_header_upload(GLuint buffer, GLintptr offset,
                                       GLsizeiptr size, const void *data)
{
    perf_rs_upload_note(buffer, offset, size, data);
    if (g_perf_emitter_header_cache_on && buffer && buffer < PERF_EMITTER_HEADER_BUFFERS) {
        perf_emitter_header_history *history = &g_perf_emitter_header_history[buffer];
        history->upload_frame = g_frame_count;
        history->upload_offset = (uint64_t)offset;
        history->upload_size = (uint64_t)size;
        memset(history->upload_prefix, 0, sizeof history->upload_prefix);
        if (data && size >= 16) memcpy(history->upload_prefix, data, 16);
    }
    if (!g_perf_emitter_header_cache_on || !buffer || buffer >= 65536 ||
        !data || offset < 0 || size < 16 || size > 2 * 1024 * 1024 ||
        offset > g_buffer_size[buffer] || size > g_buffer_size[buffer] - offset ||
        !perf_emitter_header_matches(buffer, offset, data) || bindless_map_find(buffer))
        return;
    /* Only scan a recognized pool upload, never ordinary per-frame matrices. */
    for (GLsizeiptr at = 0; at <= size - 16; at += 8) {
        const unsigned char *p = (const unsigned char *)data + at;
        if (!perf_emitter_header_matches(buffer, offset + at, p)) continue;
        perf_emitter_header_store(buffer, offset + at, p);
    }
}

static int perf_emitter_header_copy(GLuint buffer, GLintptr offset, void *data)
{
    if (!g_perf_emitter_header_cache_on) return 0;
    if (buffer && buffer < 65536 && offset >= 0 &&
        offset <= g_buffer_size[buffer] && 16 <= g_buffer_size[buffer] - offset &&
        !bindless_map_find(buffer)) {
        const perf_emitter_header_pool *pool = &g_perf_emitter_headers[buffer];
        unsigned slot = perf_emitter_header_lower_bound(pool, offset);
        if (slot < pool->count && pool->entries[slot].offset == offset) {
            memcpy(data, pool->entries[slot].pointers, 16);
            g_perf_emitter_header_hits++;
            return 1;
        }
    }
    g_perf_emitter_header_misses++;
    return 0;
}

static void perf_emitter_header_context_change(HGLRC context)
{
    perf_rs_upload_context_change(context);
    if (!g_perf_emitter_header_cache_on || g_perf_emitter_header_context == context) return;
    for (unsigned i = 0; i < PERF_EMITTER_HEADER_BUFFERS; i++)
        free(g_perf_emitter_headers[i].entries);
    memset(g_perf_emitter_headers, 0, sizeof g_perf_emitter_headers);
    memset(g_perf_emitter_header_history, 0, sizeof g_perf_emitter_header_history);
    g_perf_emitter_header_context = context;
}

static void perf_emitter_header_record_read(GLuint buffer, GLintptr offset,
                                            const void *data, uint64_t ticks)
{
    for (unsigned i = 0; i < PERF_EMITTER_READ_RECORD_CAP; i++) {
        perf_emitter_read_record *r = &g_perf_emitter_read_records[i];
        if (r->buffer && (r->buffer != buffer || r->offset != (uint64_t)offset)) continue;
        r->sequence++;
        __asm__ __volatile__("" ::: "memory");
        if (!r->buffer) r->first_frame = g_frame_count;
        r->buffer = buffer; r->offset = (uint64_t)offset; r->program = g_current_program;
        r->backing = buffer < (1u << 20) ? (uint64_t)g_buffer_size[buffer] : 0;
        r->mapped = bindless_map_find(buffer) != NULL;
        r->cached_headers = buffer < PERF_EMITTER_HEADER_BUFFERS ? g_perf_emitter_headers[buffer].count : 0;
        memcpy(r->pointers, data, 16);
        r->self_relative = perf_emitter_header_matches(buffer, offset, data);
        r->reads++; r->ticks += ticks; r->last_frame = g_frame_count;
        if (buffer < PERF_EMITTER_HEADER_BUFFERS) r->history = g_perf_emitter_header_history[buffer];
        __asm__ __volatile__("" ::: "memory");
        r->sequence++;
        return;
    }
    g_perf_emitter_read_record_overflow++;
}

static void WINAPI perf_emitter_clear_buffer(GLenum target, GLenum internal,
                                             GLenum format, GLenum type, const void *data)
{
    PERF_MAPPED_READ_BOUNDARY();
    static void (WINAPI *real)(GLenum, GLenum, GLenum, GLenum, const void *);
    if (!real) real = (__typeof__(real))trace_resolve("glClearBufferData");
    if (target < 65536) perf_emitter_header_invalidate(g_bound_buffer[target], 0, -1);
    if (real) real(target, internal, format, type, data);
}

static void WINAPI perf_emitter_clear_named_buffer(GLuint buffer, GLenum internal,
                                                   GLenum format, GLenum type, const void *data)
{
    PERF_MAPPED_READ_BOUNDARY();
    static void (WINAPI *real)(GLuint, GLenum, GLenum, GLenum, const void *);
    if (!real) real = (__typeof__(real))trace_resolve("glClearNamedBufferData");
    perf_emitter_header_invalidate(buffer, 0, -1);
    if (real) real(buffer, internal, format, type, data);
}

static void WINAPI perf_emitter_clear_buffer_range(GLenum target, GLenum internal,
    GLintptr offset, GLsizeiptr size, GLenum format, GLenum type, const void *data)
{
    PERF_MAPPED_READ_BOUNDARY();
    static void (WINAPI *real)(GLenum, GLenum, GLintptr, GLsizeiptr, GLenum, GLenum, const void *);
    if (!real) real = (__typeof__(real))trace_resolve("glClearBufferSubData");
    if (target < 65536) perf_emitter_header_invalidate(g_bound_buffer[target], offset, size);
    if (real) real(target, internal, offset, size, format, type, data);
}

static void WINAPI perf_emitter_clear_named_buffer_range(GLuint buffer, GLenum internal,
    GLintptr offset, GLsizeiptr size, GLenum format, GLenum type, const void *data)
{
    PERF_MAPPED_READ_BOUNDARY();
    static void (WINAPI *real)(GLuint, GLenum, GLintptr, GLsizeiptr, GLenum, GLenum, const void *);
    if (!real) real = (__typeof__(real))trace_resolve("glClearNamedBufferSubData");
    perf_emitter_header_invalidate(buffer, offset, size);
    if (real) real(buffer, internal, offset, size, format, type, data);
}
