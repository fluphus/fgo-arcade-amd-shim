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
