/* Bounded external stack sampling. Resume before DbgHelp or file output. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <dbghelp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define THREAD_CAP 256
#define ACTIVE_CAP 4
#define STACK_CAP 65536
typedef struct {
    DWORD id;
    HANDLE handle;
    uint64_t before, recent, start;
} sampled_thread;
static HANDLE process;
static unsigned char stack_bytes[STACK_CAP];
static DWORD64 stack_base;
static SIZE_T stack_length;
static LARGE_INTEGER frequency;
static DWORD64 program_address;

static uint64_t thread_time(HANDLE thread)
{
    FILETIME c, e, k, u;
    if (!GetThreadTimes(thread, &c, &e, &k, &u)) return 0;
    return (((uint64_t)k.dwHighDateTime << 32) | k.dwLowDateTime) +
           (((uint64_t)u.dwHighDateTime << 32) | u.dwLowDateTime);
}
static double clock_seconds(void)
{
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return (double)now.QuadPart / frequency.QuadPart;
}
static BOOL CALLBACK read_snapshot(HANDLE h, DWORD64 address, PVOID out,
                                   DWORD bytes, LPDWORD done)
{
    SIZE_T got = 0;
    *done = 0;
    if (address >= stack_base && address - stack_base < STACK_CAP) {
        if (address - stack_base > stack_length ||
            bytes > stack_length - (SIZE_T)(address - stack_base)) return FALSE;
        memcpy(out, stack_bytes + (SIZE_T)(address - stack_base), bytes);
        *done = bytes;
        return TRUE;
    }
    BOOL ok = ReadProcessMemory(h, (LPCVOID)(uintptr_t)address, out, bytes, &got);
    *done = (DWORD)got;
    return ok;
}
static void sample(FILE *out, sampled_thread *thread, double start)
{
    CONTEXT context;
    memset(&context, 0, sizeof context);
    context.ContextFlags = CONTEXT_FULL;
    double begin = clock_seconds();
    DWORD previous = SuspendThread(thread->handle);
    if (previous == (DWORD)-1) return;
    BOOL ok = GetThreadContext(thread->handle, &context);
    DWORD program = 0;
    SIZE_T program_bytes = 0;
    BOOL program_ok = program_address && ReadProcessMemory(process,
        (LPCVOID)(uintptr_t)program_address, &program, sizeof program, &program_bytes)
        && program_bytes == sizeof program;
    stack_base = context.Rsp;
    stack_length = 0;
    if (ok) {
        MEMORY_BASIC_INFORMATION info;
        if (VirtualQueryEx(process, (LPCVOID)(uintptr_t)stack_base, &info, sizeof info)) {
            SIZE_T available = (uintptr_t)info.BaseAddress + info.RegionSize - stack_base;
            if (available > STACK_CAP) available = STACK_CAP;
            ReadProcessMemory(process, (LPCVOID)(uintptr_t)stack_base,
                              stack_bytes, available, &stack_length);
        }
    }
    DWORD resumed = ResumeThread(thread->handle);
    double held = clock_seconds() - begin;
    if (resumed == (DWORD)-1) {
        fprintf(stderr, "ResumeThread failed for %lu: %lu\n", thread->id, GetLastError());
        exit(3);
    }
    if (!ok) return;
    STACKFRAME64 frame;
    memset(&frame, 0, sizeof frame);
    frame.AddrPC.Offset = context.Rip;
    frame.AddrFrame.Offset = context.Rbp;
    frame.AddrStack.Offset = context.Rsp;
    frame.AddrPC.Mode = frame.AddrFrame.Mode = frame.AddrStack.Mode = AddrModeFlat;
    fprintf(out, "{\"type\":\"sample\",\"t\":%.6f,\"tid\":%lu,\"held_us\":%.1f",
            begin - start, thread->id, held * 1e6);
    if (program_ok) fprintf(out, ",\"app_program\":%lu", program);
    fprintf(out, ",\"frames\":[\"0x%llx\"", (unsigned long long)context.Rip);
    DWORD64 last_pc = context.Rip, last_sp = context.Rsp;
    for (int i = 0; i < 24 && stack_length; i++) {
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread->handle, &frame,
                         &context, read_snapshot, SymFunctionTableAccess64,
                         SymGetModuleBase64, NULL) || !frame.AddrPC.Offset) break;
        if (frame.AddrPC.Offset == last_pc && frame.AddrStack.Offset == last_sp) continue;
        fprintf(out, ",\"0x%llx\"", (unsigned long long)frame.AddrPC.Offset);
        last_pc = frame.AddrPC.Offset;
        last_sp = frame.AddrStack.Offset;
    }
    fprintf(out, "]}\n");
}
static int by_cpu(const void *a, const void *b)
{
    const sampled_thread *x = a, *y = b;
    uint64_t dx = x->recent - x->before, dy = y->recent - y->before;
    return dx < dy ? 1 : dx > dy ? -1 : 0;
}
int main(int argc, char **argv)
{
    if (argc != 4 && argc != 5) { fprintf(stderr, "usage: battle_cpu_sample PID SECONDS OUTPUT.jsonl [APP_PROGRAM_ADDRESS]\n"); return 1; }
    DWORD pid = strtoul(argv[1], NULL, 10);
    double seconds = strtod(argv[2], NULL);
    if (!pid || seconds <= 0 || seconds > 15) return 1;
    if (argc == 5) {
        char *end = NULL;
        program_address = strtoull(argv[4], &end, 0);
        if (!program_address || !end || *end) return 1;
    }
    QueryPerformanceFrequency(&frequency);
    process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!process) { fprintf(stderr, "OpenProcess: %lu\n", GetLastError()); return 1; }
    SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_FAIL_CRITICAL_ERRORS |
                  SYMOPT_NO_PROMPTS | SYMOPT_IGNORE_NT_SYMPATH);
    if (!SymInitialize(process, ".", TRUE)) return 1;
    sampled_thread threads[THREAD_CAP];
    unsigned count = 0;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    THREADENTRY32 entry = {.dwSize = sizeof entry};
    for (BOOL ok = Thread32First(snapshot, &entry); ok; ok = Thread32Next(snapshot, &entry)) {
        if (entry.th32OwnerProcessID != pid || count == THREAD_CAP) continue;
        HANDLE thread = OpenThread(THREAD_QUERY_INFORMATION | THREAD_GET_CONTEXT |
                                   THREAD_SUSPEND_RESUME, FALSE, entry.th32ThreadID);
        if (!thread) continue;
        threads[count++] = (sampled_thread){entry.th32ThreadID, thread, thread_time(thread), 0, 0};
    }
    CloseHandle(snapshot);
    Sleep(250);
    for (unsigned i = 0; i < count; i++) threads[i].recent = thread_time(threads[i].handle);
    qsort(threads, count, sizeof threads[0], by_cpu);
    unsigned active = count < ACTIVE_CAP ? count : ACTIVE_CAP;
    FILE *out = fopen(argv[3], "wb");
    if (!out) return 1;
    for (unsigned i = 0; i < active; i++) threads[i].start = thread_time(threads[i].handle);
    double start = clock_seconds(), next = start;
    while (clock_seconds() - start < seconds) {
        for (unsigned i = 0; i < active; i++) sample(out, &threads[i], start);
        next += 0.020;
        double delay = next - clock_seconds();
        if (delay > 0) Sleep((DWORD)(delay * 1000));
    }
    double elapsed = clock_seconds() - start;
    for (unsigned i = 0; i < active; i++)
        fprintf(out, "{\"type\":\"cpu\",\"tid\":%lu,\"elapsed\":%.6f,\"cpu_ms\":%.3f}\n",
                threads[i].id, elapsed, (thread_time(threads[i].handle) - threads[i].start) / 10000.0);
    fclose(out);
    for (unsigned i = 0; i < count; i++) CloseHandle(threads[i].handle);
    SymCleanup(process);
    CloseHandle(process);
    printf("Sampled %u threads for %.3fs into %s\n", active, elapsed, argv[3]);
    return 0;
}
