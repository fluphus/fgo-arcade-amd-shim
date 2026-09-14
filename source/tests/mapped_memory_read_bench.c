#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <smmintrin.h>

static volatile uint64_t sink;
/* Keep the vector load when a caller consumes only one part of its result. */
static __attribute__((noinline)) __m128i stream_load(const unsigned char *source)
{
    return _mm_stream_load_si128((__m128i *)source);
}
static void stream_copy(unsigned char *dst, const unsigned char *src, size_t size)
{
    size_t i=0;
    for (; i+64<=size; i+=64) {
        __m128i a=_mm_stream_load_si128((__m128i *)(src+i));
        __m128i b=_mm_stream_load_si128((__m128i *)(src+i+16));
        __m128i c=_mm_stream_load_si128((__m128i *)(src+i+32));
        __m128i d=_mm_stream_load_si128((__m128i *)(src+i+48));
        _mm_storeu_si128((__m128i *)(dst+i),a);
        _mm_storeu_si128((__m128i *)(dst+i+16),b);
        _mm_storeu_si128((__m128i *)(dst+i+32),c);
        _mm_storeu_si128((__m128i *)(dst+i+48),d);
    }
    if (i<size) memcpy(dst+i,src+i,size-i);
}

/* Mode 0: repeated mapped reads. 1/2: one copy per round, then RAM reads.
   The measured interval includes each copy, not just the cheap cache hits. */
__declspec(dllexport) double TestMemoryReadBenchmark(const void *mapped, size_t size,
    unsigned rounds, unsigned repeats, int mode, uint64_t *checksum)
{
    unsigned char *copy=malloc(size);
    if (!copy) return -1;
    LARGE_INTEGER frequency,begin,end;
    QueryPerformanceFrequency(&frequency); QueryPerformanceCounter(&begin);
    uint64_t sum=0;
    for (unsigned round=0;round<rounds;round++) {
        const unsigned char *source=mapped;
        if (mode==1) { memcpy(copy,mapped,size); source=copy; }
        if (mode==2) { stream_copy(copy,mapped,size); source=copy; }
        for (unsigned pass=0;pass<repeats;pass++) {
            for (size_t i=0;i+64<=size;i+=256) {
                uint32_t a; uint64_t b,c[2];
                if (mode>=3) {
                    if (mode==4) _mm_mfence();
                    __m128i va=stream_load(source+i);
                    __m128i vb=stream_load(source+i+16);
                    __m128i vc=stream_load(source+i+32);
                    a=(uint32_t)_mm_cvtsi128_si32(va);
                    b=(uint64_t)_mm_cvtsi128_si64(vb);
                    _mm_storeu_si128((__m128i *)c,vc);
                } else {
                    memcpy(&a,source+i,4); memcpy(&b,source+i+16,8);
                    memcpy(c,source+i+32,16);
                }
                sum+=a+b+c[0]+c[1];
            }
        }
        sink=sum;
    }
    QueryPerformanceCounter(&end); *checksum=sum; free(copy);
    return 1000.0*(end.QuadPart-begin.QuadPart)/frequency.QuadPart;
}
