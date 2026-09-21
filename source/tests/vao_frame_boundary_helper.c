#include <windows.h>

static volatile unsigned restores;

__declspec(dllexport) void WINAPI restore_vao(void (WINAPI *bind)(unsigned))
{
    bind(2);
    bind(0);
    restores++;
}
