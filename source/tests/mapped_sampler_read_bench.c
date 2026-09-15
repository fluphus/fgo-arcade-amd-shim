#include "regular_sampler_wrapper.c"
_Static_assert(sizeof(bindless_map_rec) == 48, "map observer layout changed");

__declspec(dllexport) double TestMappedSamplerRead(void *ptr, size_t backing,
    size_t offset, size_t size, unsigned repeats, int stream, uint64_t *checksum)
{
    unsigned char bytes[256];
    bindless_map_rec map = {0};
    LARGE_INTEGER frequency, begin, end;
    uint64_t sum = 0;
    if (!ptr || size == 0 || size > sizeof bytes || offset + size > backing ||
        ((uintptr_t)ptr & 15) || (backing & 15)) return -1;
    if (stream && !__builtin_cpu_supports("sse4.1")) return -2;
    map.buffer = 1; map.ptr = ptr; map.length = (GLsizeiptr)backing;
    map.access = 0x0002; map.active = 1;
    map.sampler_wc_read = stream ? 0 : 2;
    g_perf_pointer_shadow_on = 1;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&begin);
    for (unsigned i = 0; i < repeats; i++) {
        if (!perf_rs_mapped_copy(&map, 1, (GLintptr)offset,
                                (GLsizeiptr)size, bytes)) return -3;
        sum += bytes[i % size];
    }
    QueryPerformanceCounter(&end);
    *checksum = sum;
    return 1000.0 * (double)(end.QuadPart - begin.QuadPart) / frequency.QuadPart;
}

__declspec(dllexport) int TestMappedSamplerEqual(void *ptr, size_t backing,
    size_t offset, size_t size)
{
    unsigned char normal[256], streamed[256];
    bindless_map_rec map = {0};
    if (!ptr || size == 0 || size > sizeof normal || offset + size > backing ||
        ((uintptr_t)ptr & 15) || (backing & 15) ||
        !__builtin_cpu_supports("sse4.1")) return 0;
    memcpy(normal, (unsigned char *)ptr + offset, size);
    map.buffer = 1; map.ptr = ptr; map.length = (GLsizeiptr)backing;
    map.access = 0x0002; map.active = 1;
    g_perf_pointer_shadow_on = 1;
    if (!perf_rs_mapped_copy(&map, 1, offset, size, streamed)) return 0;
    return memcmp(normal, streamed, size) == 0;
}

__declspec(dllexport) DWORD TestMappedMemoryProtect(void *ptr)
{
    MEMORY_BASIC_INFORMATION info;
    return VirtualQuery(ptr, &info, sizeof info) ? info.Protect : 0;
}

__declspec(dllexport) int TestMappedSamplerExpected(void *ptr, size_t backing,
    size_t offset, size_t size, const void *expected)
{
    unsigned char bytes[256];
    bindless_map_rec map = {0};
    if (!ptr || size == 0 || size > sizeof bytes || offset + size > backing) return 0;
    map.buffer = 1; map.ptr = ptr; map.length = (GLsizeiptr)backing;
    map.access = 0x0002; map.active = 1;
    g_perf_pointer_shadow_on = 1;
    if (!perf_rs_mapped_copy(&map, 1, offset, size, bytes)) return 0;
    return memcmp(bytes, expected, size) == 0;
}

__declspec(dllexport) double TestMappedSamplerLayout(void *ptr, size_t backing,
    const unsigned *offsets, const unsigned *sizes, unsigned count,
    unsigned repeats, int coalesce, const void *expected, uint64_t *checksum,
    unsigned *merged)
{
    unsigned char bytes[4096];
    bindless_map_rec map = {0};
    LARGE_INTEGER frequency, begin, end;
    uint64_t sum = 0;
    if (!ptr || !count || count > 4 || !repeats) return -1;
    unsigned first = offsets[0], end_offset = 0;
    for (unsigned r = 0; r < count; r++) {
        if (!sizes[r] || sizes[r] > sizeof bytes ||
            offsets[r] > sizeof bytes - sizes[r] ||
            offsets[r] + sizes[r] > backing) return -1;
        if (offsets[r] < first) first = offsets[r];
        if (offsets[r] + sizes[r] > end_offset) end_offset = offsets[r] + sizes[r];
    }
    map.buffer = 1; map.ptr = ptr; map.length = (GLsizeiptr)backing;
    map.access = 0x0002; map.active = 1;
    g_perf_pointer_shadow_on = 1;
    *merged = 0;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&begin);
    for (unsigned i = 0; i < repeats; i++) {
        perf_rs_wc_request pending[PERF_RS_SLOTS+1];
        int pending_count=0;
        int copied = coalesce && end_offset-first>16 && end_offset-first<=64 &&
            perf_rs_queue_mapped_read(&map,first,end_offset-first,bytes+first,
                                     pending,&pending_count);
        if (copied) perf_rs_wc_read_batch(pending,pending_count);
        *merged += copied;
        if (!copied) {
            for (unsigned r = 0; r < count; r++) {
                if (!perf_rs_mapped_copy(&map, 1, offsets[r], sizes[r],
                                        bytes + offsets[r])) return -2;
            }
        }
        for (unsigned r = 0; r < count; r++)
            sum += bytes[offsets[r] + i % sizes[r]];
    }
    QueryPerformanceCounter(&end);
    for (unsigned r = 0; r < count; r++)
        if (memcmp(bytes + offsets[r], (const unsigned char *)expected + offsets[r],
                   sizes[r])) return -3;
    *checksum = sum;
    return 1000.0 * (double)(end.QuadPart - begin.QuadPart) / frequency.QuadPart;
}

__declspec(dllexport) double TestMappedSamplerBatch(void *ptr, size_t backing,
    const unsigned *offsets, const unsigned *sizes, unsigned count,
    unsigned repeats, int batch, const void *expected, uint64_t *checksum,
    unsigned *queued)
{
    unsigned char bytes[4096];
    bindless_map_rec map={0};
    LARGE_INTEGER frequency,begin,end;
    uint64_t sum=0;
    if (!ptr || !count || count>4 || !repeats) return -1;
    for (unsigned r=0;r<count;r++)
        if (!sizes[r] || sizes[r]>sizeof bytes || offsets[r]>sizeof bytes-sizes[r] ||
            offsets[r]+sizes[r]>backing) return -1;
    map.buffer=1; map.ptr=ptr; map.length=(GLsizeiptr)backing;
    map.access=0x0002; map.active=1; g_perf_pointer_shadow_on=1;
    QueryPerformanceFrequency(&frequency);
    *queued=0;
    QueryPerformanceCounter(&begin);
    for (unsigned i=0;i<repeats;i++) {
        perf_rs_wc_request pending[PERF_RS_SLOTS+1];
        int pending_count=0;
        for (unsigned r=0;r<count;r++) {
            if (batch && perf_rs_queue_mapped_read(&map,offsets[r],sizes[r],
                    bytes+offsets[r],pending,&pending_count)) continue;
            if (!perf_rs_mapped_copy(&map,1,offsets[r],sizes[r],bytes+offsets[r])) return -2;
        }
        if (pending_count) perf_rs_wc_read_batch(pending,pending_count);
        *queued+=(unsigned)pending_count;
        for (unsigned r=0;r<count;r++) sum+=bytes[offsets[r]+i%sizes[r]];
    }
    QueryPerformanceCounter(&end);
    for (unsigned r=0;r<count;r++)
        if (memcmp(bytes+offsets[r],(const unsigned char *)expected+offsets[r],sizes[r])) return -3;
    *checksum=sum;
    return 1000.0*(double)(end.QuadPart-begin.QuadPart)/frequency.QuadPart;
}
