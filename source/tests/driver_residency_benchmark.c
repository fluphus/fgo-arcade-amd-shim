#include <windows.h>
typedef void (WINAPI *draw_arrays_t)(unsigned, int, int);

__declspec(dllexport) double TestResidentDraws(draw_arrays_t draw, unsigned count)
{
    LARGE_INTEGER start, end, frequency;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&start);
    for (unsigned i = 0; i < count; i++) draw(4, 0, 3);
    QueryPerformanceCounter(&end);
    return 1000.0 * (double)(end.QuadPart - start.QuadPart) / (double)frequency.QuadPart;
}
