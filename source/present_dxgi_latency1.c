#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dxgi1_3.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "present_dxgi_latency1.h"

#define DXGI_PRESENT_DO_NOT_WAIT_FLAG 0x00000008u
#define DXGI_PRESENT_ALLOW_TEARING_FLAG 0x00000200u

typedef struct DxGuid {
    unsigned long data1;
    unsigned short data2;
    unsigned short data3;
    unsigned char data4[8];
} DxGuid;

static const DxGuid k_iid_dxgi_device1 = {
    0x77db970f, 0x6276, 0x48ba,
    {0xba, 0x28, 0x07, 0x01, 0x43, 0xb4, 0x39, 0x2c}
};

typedef HRESULT (WINAPI *CreateFactory_fn)(const DxGuid *iid, void **factory);
typedef HRESULT (WINAPI *CreateFactory2_fn)(UINT flags, const DxGuid *iid, void **factory);
typedef HRESULT (STDMETHODCALLTYPE *CreateSwapChain_fn)(void *factory, void *device, void *desc, void **swapchain);
typedef HRESULT (STDMETHODCALLTYPE *CreateSwapChainForHwnd_fn)(
    void *factory, void *device, HWND window, const void *desc,
    const void *fullscreen_desc, void *restrict_output, void **swapchain);
typedef HRESULT (STDMETHODCALLTYPE *Present_fn)(void *swapchain, UINT sync, UINT flags);
typedef HRESULT (STDMETHODCALLTYPE *Present1_fn)(void *swapchain, UINT sync, UINT flags, const void *params);
typedef HRESULT (STDMETHODCALLTYPE *GetDevice_fn)(void *swapchain, const DxGuid *iid, void **device);
typedef HRESULT (STDMETHODCALLTYPE *SetLatency_fn)(void *device, UINT latency);
typedef ULONG (STDMETHODCALLTYPE *Release_fn)(void *object);

static CreateFactory_fn g_real_factory;
static CreateFactory_fn g_real_factory1;
static CreateFactory2_fn g_real_factory2;
static CreateSwapChain_fn g_real_create_chain;
static CreateSwapChainForHwnd_fn g_real_create_hwnd;
static Present_fn g_real_present;
static Present1_fn g_real_present1;
static int g_enabled;
static int g_latency_set;
static LONG g_install_state;
static int g_present_logs;
#ifdef PRESENT_DXGI_LATENCY1_TEST
static int g_dxgi_log_silent;
#endif

static void dxgi_log(const char *text)
{
    FILE *file;
#ifdef PRESENT_DXGI_LATENCY1_TEST
    if (g_dxgi_log_silent) return;
#endif
    CreateDirectoryA("C:\\fgo\\_tools", NULL);
    CreateDirectoryA("C:\\fgo\\_tools\\glshim", NULL);
    file = fopen("C:\\fgo\\_tools\\glshim\\dxgi_latency1.log", "a");
    if (!file) return;
    fprintf(file, "%s\n", text);
    fclose(file);
}

int present_dxgi_latency1_enabled(void)
{
    if (GetFileAttributesA("C:\\fgo\\_tools\\glshim\\dxgi_latency1.off") != INVALID_FILE_ATTRIBUTES)
        return 0;
    return 1;
}

#ifndef PRESENT_DXGI_LATENCY1_TEST
static void set_chain_latency(void *swapchain)
{
    IDXGISwapChain2 *chain2 = NULL;
    HRESULT hr = IDXGISwapChain1_QueryInterface((IDXGISwapChain1 *)swapchain,
                                                &IID_IDXGISwapChain2, (void **)&chain2);
    char line[160];
    if (hr >= 0 && chain2) {
        hr = IDXGISwapChain2_SetMaximumFrameLatency(chain2, 1);
        if (hr >= 0) g_latency_set = 1;
        IDXGISwapChain2_Release(chain2);
    }
    _snprintf(line, sizeof line, "chain latency=1 hr=0x%08lx",
              (unsigned long)hr);
    line[sizeof line - 1] = 0;
    dxgi_log(line);
}
#endif

static void *vtable_slot(void *object, int index)
{
    void **table;
    if (!object) return NULL;
    table = *(void ***)object;
    if (!table) return NULL;
    return table[index];
}

static int replace_slot(void *object, int index, void *replacement, void **previous)
{
    void **table;
    DWORD old_protect;
    void *prior;
    if (!object) return 0;
    table = *(void ***)object;
    if (!table || !table[index]) return 0;
    prior = table[index];
    if (prior == replacement) {
        if (previous && !*previous) *previous = prior;
        return 1;
    }
    if (!VirtualProtect(&table[index], sizeof(void *), PAGE_READWRITE, &old_protect))
        return 0;
    if (previous && !*previous) *previous = prior;
    table[index] = replacement;
    VirtualProtect(&table[index], sizeof(void *), old_protect, &old_protect);
    return 1;
}

static void note_present(UINT original_sync, UINT original_flags, UINT sync, UINT flags)
{
    char line[192];
    int count;
    count = g_present_logs;
    if (count >= 6) return;
    g_present_logs = count + 1;
    _snprintf(line, sizeof line,
              "present original_sync=%u original_flags=0x%x sync=%u flags=0x%x",
              original_sync, original_flags, sync, flags);
    line[sizeof line - 1] = 0;
    dxgi_log(line);
}

static void ensure_latency(void *swapchain)
{
    GetDevice_fn get_device;
    SetLatency_fn set_latency;
    Release_fn release_device;
    void *device = NULL;
    char line[128];
    HRESULT result;
    if (g_latency_set || !swapchain) return;
    get_device = (GetDevice_fn)vtable_slot(swapchain, 7);
    if (!get_device) return;
    result = get_device(swapchain, &k_iid_dxgi_device1, &device);
    if (result < 0 || !device) {
        _snprintf(line, sizeof line, "GetDevice IDXGIDevice1 hr=0x%08lx", (unsigned long)result);
        line[sizeof line - 1] = 0;
        dxgi_log(line);
        g_latency_set = 1;
        return;
    }
    set_latency = (SetLatency_fn)vtable_slot(device, 12);
    result = set_latency ? set_latency(device, 1) : E_FAIL;
    _snprintf(line, sizeof line, "SetMaximumFrameLatency(1) hr=0x%08lx", (unsigned long)result);
    line[sizeof line - 1] = 0;
    dxgi_log(line);
    release_device = (Release_fn)vtable_slot(device, 2);
    if (release_device) release_device(device);
    g_latency_set = 1;
}

static UINT rewrite_flags(UINT flags)
{
    flags &= ~DXGI_PRESENT_ALLOW_TEARING_FLAG;
    flags &= ~DXGI_PRESENT_DO_NOT_WAIT_FLAG;
    return flags;
}

static HRESULT STDMETHODCALLTYPE hook_present(void *swapchain, UINT sync, UINT flags)
{
    UINT original_sync = sync;
    UINT original_flags = flags;
    if (!g_real_present) return E_FAIL;
    if (g_enabled) {
        ensure_latency(swapchain);
        flags = rewrite_flags(flags);
        if (sync == 0) sync = 1;
        note_present(original_sync, original_flags, sync, flags);
    }
    return g_real_present(swapchain, sync, flags);
}

static HRESULT STDMETHODCALLTYPE hook_present1(void *swapchain, UINT sync, UINT flags, const void *params)
{
    UINT original_sync = sync;
    UINT original_flags = flags;
    if (!g_real_present1) return E_FAIL;
    if (g_enabled) {
        ensure_latency(swapchain);
        flags = rewrite_flags(flags);
        if (sync == 0) sync = 1;
        note_present(original_sync, original_flags, sync, flags);
    }
    return g_real_present1(swapchain, sync, flags, params);
}

static void hook_swapchain(void *swapchain)
{
    if (!swapchain) return;
    replace_slot(swapchain, 8, (void *)hook_present, (void **)&g_real_present);
    replace_slot(swapchain, 22, (void *)hook_present1, (void **)&g_real_present1);
}

static HRESULT STDMETHODCALLTYPE hook_create_chain(void *factory, void *device, void *desc, void **swapchain)
{
    HRESULT result;
    char line[160];
    if (!g_real_create_chain) return E_FAIL;
    result = g_real_create_chain(factory, device, desc, swapchain);
    if (result >= 0 && swapchain && *swapchain) {
        _snprintf(line, sizeof line, "CreateSwapChain chain=%p pid=%lu",
                  *swapchain, (unsigned long)GetCurrentProcessId());
        line[sizeof line - 1] = 0;
        dxgi_log(line);
        hook_swapchain(*swapchain);
    }
    return result;
}

static HRESULT STDMETHODCALLTYPE hook_create_hwnd(
    void *factory, void *device, HWND window, const void *desc,
    const void *fullscreen_desc, void *restrict_output, void **swapchain)
{
    HRESULT result;
    HWND target = window;
    char line[192];
    if (!g_real_create_hwnd) return E_FAIL;
    {
#ifndef PRESENT_DXGI_LATENCY1_TEST
        DXGI_SWAP_CHAIN_DESC1 candidate;
        if (desc) {
            candidate = *(const DXGI_SWAP_CHAIN_DESC1 *)desc;
            candidate.Flags |= DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
            result = g_real_create_hwnd(factory, device, target, &candidate,
                                        fullscreen_desc, restrict_output, swapchain);
            if (result < 0) {
                dxgi_log("waitable chain creation rejected; retrying unchanged descriptor");
                result = g_real_create_hwnd(factory, device, target, desc,
                                            fullscreen_desc, restrict_output, swapchain);
            } else if (swapchain && *swapchain) {
                set_chain_latency(*swapchain);
            }
        } else
#endif
        result = g_real_create_hwnd(factory, device, target, desc,
                                    fullscreen_desc, restrict_output, swapchain);
    }
    _snprintf(line, sizeof line, "CreateSwapChainForHwnd chain=%p requested_hwnd=%p actual_hwnd=%p hr=0x%08lx pid=%lu",
              result >= 0 && swapchain ? *swapchain : NULL, (void *)window, (void *)target,
              (unsigned long)result, (unsigned long)GetCurrentProcessId());
    line[sizeof line - 1] = 0;
    dxgi_log(line);
    if (result >= 0 && swapchain && *swapchain) {
        hook_swapchain(*swapchain);
    }
    return result;
}

static void wrap_factory(void *factory)
{
    if (!factory) return;
    replace_slot(factory, 10, (void *)hook_create_chain, (void **)&g_real_create_chain);
    replace_slot(factory, 15, (void *)hook_create_hwnd, (void **)&g_real_create_hwnd);
}

static int install_jump(void *target, const unsigned char *expected, int steal, void *detour, void **trampoline_out)
{
    unsigned char *target_bytes = (unsigned char *)target;
    unsigned char *trampoline;
    DWORD old_protect;
    int index;
    if (!target || steal < 14 || steal > 32) return 0;
    if (memcmp(target_bytes, expected, (size_t)steal) != 0) return 0;
    trampoline = (unsigned char *)VirtualAlloc(NULL, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!trampoline) return 0;
    memcpy(trampoline, target_bytes, (size_t)steal);
    trampoline[steal] = 0xFF;
    trampoline[steal + 1] = 0x25;
    trampoline[steal + 2] = 0;
    trampoline[steal + 3] = 0;
    trampoline[steal + 4] = 0;
    trampoline[steal + 5] = 0;
    *(uint64_t *)(trampoline + steal + 6) = (uint64_t)(target_bytes + steal);
    if (!VirtualProtect(target_bytes, (size_t)steal, PAGE_EXECUTE_READWRITE, &old_protect))
        return 0;
    target_bytes[0] = 0xFF;
    target_bytes[1] = 0x25;
    target_bytes[2] = 0;
    target_bytes[3] = 0;
    target_bytes[4] = 0;
    target_bytes[5] = 0;
    *(uint64_t *)(target_bytes + 6) = (uint64_t)detour;
    for (index = 14; index < steal; index++) target_bytes[index] = 0x90;
    VirtualProtect(target_bytes, (size_t)steal, old_protect, &old_protect);
    FlushInstructionCache(GetCurrentProcess(), target_bytes, (size_t)steal);
    *trampoline_out = trampoline;
    return 1;
}

static HRESULT WINAPI hook_factory(const DxGuid *iid, void **factory)
{
    HRESULT result;
    result = g_real_factory(iid, factory);
    if (result >= 0 && factory && *factory) wrap_factory(*factory);
    return result;
}

static HRESULT WINAPI hook_factory1(const DxGuid *iid, void **factory)
{
    HRESULT result;
    result = g_real_factory1(iid, factory);
    if (result >= 0 && factory && *factory) wrap_factory(*factory);
    return result;
}

static HRESULT WINAPI hook_factory2(UINT flags, const DxGuid *iid, void **factory)
{
    HRESULT result;
    result = g_real_factory2(flags, iid, factory);
    if (result >= 0 && factory && *factory) wrap_factory(*factory);
    return result;
}

void present_dxgi_latency1_install(int allow_load)
{
    HMODULE dxgi;
    void *factory;
    void *factory1;
    void *factory2;
    static const unsigned char k_factory_prefix[] = {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xDA, 0x4C, 0x8B, 0xC2
    };
    static const unsigned char k_factory2_prefix[] = {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20
    };
    char line[160];
    int hooked = 0;
    if (!present_dxgi_latency1_enabled()) {
        if (InterlockedCompareExchange(&g_install_state, 2, 0) == 0)
            dxgi_log("dxgi_latency1 disabled by off file");
        return;
    }
    if (InterlockedCompareExchange(&g_install_state, 1, 0) != 0) return;
    g_enabled = 1;
    dxgi = GetModuleHandleA("dxgi.dll");
    if (!dxgi && allow_load) dxgi = LoadLibraryA("dxgi.dll");
    if (!dxgi) {
        InterlockedExchange(&g_install_state, 0);
        return;
    }
    factory = (void *)GetProcAddress(dxgi, "CreateDXGIFactory");
    factory1 = (void *)GetProcAddress(dxgi, "CreateDXGIFactory1");
    factory2 = (void *)GetProcAddress(dxgi, "CreateDXGIFactory2");
    if (factory && install_jump(factory, k_factory_prefix, 16, (void *)hook_factory, (void **)&g_real_factory))
        hooked++;
    if (factory1 && install_jump(factory1, k_factory_prefix, 16, (void *)hook_factory1, (void **)&g_real_factory1))
        hooked++;
    if (factory2 && install_jump(factory2, k_factory2_prefix, 15, (void *)hook_factory2, (void **)&g_real_factory2))
        hooked++;
    _snprintf(line, sizeof line, "dxgi_latency1 install hooked=%d marker=dxgi-latency1 occluder=off limiter=kept", hooked);
    line[sizeof line - 1] = 0;
    dxgi_log(line);
    if (!hooked) dxgi_log("prologue mismatch; interval rewrite not installed");
}

#ifdef PRESENT_DXGI_LATENCY1_TEST
typedef struct FakeDevice {
    void **vt;
    unsigned latency;
    int latency_calls;
    int release_calls;
    int other_calls;
} FakeDevice;
typedef struct FakeChain {
    void **vt;
    FakeDevice *device;
    unsigned payload;
    int presents;
    unsigned last_sync;
    unsigned last_flags;
    const void *last_params;
    int other_calls;
} FakeChain;
typedef struct FakeFactory {
    void **vt;
    int other_calls;
    int create_calls;
    void *last_device;
    const void *last_desc;
    HWND last_window;
    const void *last_fullscreen;
    void *last_restrict;
} FakeFactory;

static FakeChain g_fake_chain;

static HRESULT STDMETHODCALLTYPE fake_device_other(FakeDevice *self)
{
    self->other_calls++;
    return 0;
}
static ULONG STDMETHODCALLTYPE fake_device_release(FakeDevice *self)
{
    self->release_calls++;
    return 1;
}
static HRESULT STDMETHODCALLTYPE fake_device_latency(FakeDevice *self, UINT latency)
{
    self->latency = latency;
    self->latency_calls++;
    return 0;
}
static HRESULT STDMETHODCALLTYPE fake_chain_other(FakeChain *self)
{
    self->other_calls++;
    return 0;
}
static HRESULT STDMETHODCALLTYPE fake_chain_present(FakeChain *self, UINT sync, UINT flags)
{
    self->presents++;
    self->last_sync = sync;
    self->last_flags = flags;
    return 0x4C41544C;
}
static HRESULT STDMETHODCALLTYPE fake_chain_present1(FakeChain *self, UINT sync, UINT flags, const void *params)
{
    self->last_params = params;
    return fake_chain_present(self, sync, flags);
}
static HRESULT STDMETHODCALLTYPE fake_chain_get_device(FakeChain *self, const DxGuid *iid, void **out)
{
    if (!iid || iid->data1 != 0x77db970f || !out) return E_NOINTERFACE;
    *out = self->device;
    return 0;
}
static HRESULT STDMETHODCALLTYPE fake_chain_get_device_fail(FakeChain *self, const DxGuid *iid, void **out)
{
    (void)self;
    (void)iid;
    (void)out;
    return E_NOINTERFACE;
}
static HRESULT STDMETHODCALLTYPE fake_factory_other(FakeFactory *self)
{
    self->other_calls++;
    return 0;
}
static HRESULT STDMETHODCALLTYPE fake_factory_create(FakeFactory *self, void *device, void *desc, void **swapchain)
{
    self->last_device = device;
    self->last_desc = desc;
    self->create_calls++;
    *swapchain = &g_fake_chain;
    return 0;
}
static HRESULT STDMETHODCALLTYPE fake_factory_create_hwnd(
    FakeFactory *self, void *device, HWND window, const void *desc,
    const void *fullscreen_desc, void *restrict_output, void **swapchain)
{
    self->last_device = device;
    self->last_window = window;
    self->last_desc = desc;
    self->last_fullscreen = fullscreen_desc;
    self->last_restrict = restrict_output;
    self->create_calls++;
    *swapchain = &g_fake_chain;
    return 0;
}

int present_dxgi_latency1_semantics_test(void)
{
    FakeDevice device;
    FakeFactory factory;
    void *device_slots[16];
    void *chain_slots[24];
    void *factory_slots[24];
    const unsigned char params[16] = {1, 2, 3, 4};
    int slot;
    HRESULT result;
    void *created = NULL;

    g_dxgi_log_silent = 1;
    g_enabled = 1;
    g_latency_set = 0;
    g_present_logs = 0;
    g_real_present = NULL;
    g_real_present1 = NULL;
    g_real_create_chain = NULL;
    g_real_create_hwnd = NULL;
    memset(&device, 0, sizeof device);
    memset(&g_fake_chain, 0, sizeof g_fake_chain);
    memset(&factory, 0, sizeof factory);
    device.vt = device_slots;
    g_fake_chain.vt = chain_slots;
    factory.vt = factory_slots;
    g_fake_chain.device = &device;
    g_fake_chain.payload = 0x00C0FFEE;
    for (slot = 0; slot < 16; slot++) device_slots[slot] = (void *)fake_device_other;
    for (slot = 0; slot < 24; slot++) factory_slots[slot] = (void *)fake_factory_other;
    device_slots[2] = (void *)fake_device_release;
    device_slots[12] = (void *)fake_device_latency;
    for (slot = 0; slot < 24; slot++) chain_slots[slot] = (void *)fake_chain_other;
    chain_slots[7] = (void *)fake_chain_get_device;
    chain_slots[8] = (void *)fake_chain_present;
    chain_slots[22] = (void *)fake_chain_present1;
    factory_slots[10] = (void *)fake_factory_create;
    factory_slots[15] = (void *)fake_factory_create_hwnd;

    hook_swapchain(&g_fake_chain);
    if (chain_slots[8] != (void *)hook_present) return 1;
    if (chain_slots[22] != (void *)hook_present1) return 2;
    if (chain_slots[7] != (void *)fake_chain_get_device) return 3;
    if (chain_slots[9] != (void *)fake_chain_other) return 4;

    result = hook_present(&g_fake_chain, 0, DXGI_PRESENT_ALLOW_TEARING_FLAG | DXGI_PRESENT_DO_NOT_WAIT_FLAG);
    if (result != (HRESULT)0x4C41544C) return 5;
    if (g_fake_chain.presents != 1 || g_fake_chain.last_sync != 1 || g_fake_chain.last_flags != 0) return 6;
    if (g_fake_chain.payload != 0x00C0FFEE) return 7;
    if (device.latency_calls != 1 || device.latency != 1 || device.release_calls != 1) return 8;
    if (g_fake_chain.other_calls != 0 || device.other_calls != 0) return 9;

    result = hook_present1(&g_fake_chain, 0, DXGI_PRESENT_ALLOW_TEARING_FLAG | 0x1u, params);
    if (result != (HRESULT)0x4C41544C) return 10;
    if (g_fake_chain.last_params != params) return 11;
    if (g_fake_chain.last_sync != 1 || g_fake_chain.last_flags != 0x1u) return 12;
    if (g_fake_chain.payload != 0x00C0FFEE) return 13;
    if (device.latency_calls != 1) return 14;

    result = hook_present(&g_fake_chain, 2, 0);
    if (g_fake_chain.last_sync != 2 || g_fake_chain.last_flags != 0) return 15;

    g_enabled = 0;
    result = hook_present(&g_fake_chain, 0, DXGI_PRESENT_ALLOW_TEARING_FLAG);
    if (g_fake_chain.last_sync != 0 || g_fake_chain.last_flags != DXGI_PRESENT_ALLOW_TEARING_FLAG) return 16;
    g_enabled = 1;

    wrap_factory(&factory);
    if (factory_slots[10] != (void *)hook_create_chain) return 17;
    if (factory_slots[15] != (void *)hook_create_hwnd) return 18;
    if (factory_slots[7] != (void *)fake_factory_other || factory_slots[11] != (void *)fake_factory_other) return 19;
    result = hook_create_chain(&factory, NULL, NULL, &created);
    if (result != 0 || created != &g_fake_chain || factory.create_calls != 1) return 20;
    if (factory.other_calls != 0) return 21;
    if (g_fake_chain.payload != 0x00C0FFEE) return 22;

    {
        unsigned char desc[64];
        unsigned char desc_before[64];
        unsigned char fullscreen[32];
        unsigned char fullscreen_before[32];
        unsigned char frame[32];
        unsigned char frame_before[32];
        void *sentinel_device = (void *)(uintptr_t)0x1111;
        HWND sentinel_window = (HWND)(uintptr_t)0x2222;
        void *sentinel_restrict = (void *)(uintptr_t)0x3333;
        int index;
        for (index = 0; index < 64; index++) desc[index] = (unsigned char)(0xA5 ^ index);
        memcpy(desc_before, desc, sizeof desc);
        for (index = 0; index < 32; index++) fullscreen[index] = (unsigned char)(0x3C ^ index);
        memcpy(fullscreen_before, fullscreen, sizeof fullscreen);
        result = hook_create_chain(&factory, sentinel_device, desc, &created);
        if (result != 0 || created != &g_fake_chain) return 23;
        if (memcmp(desc, desc_before, sizeof desc) != 0) return 24;
        if (factory.last_device != sentinel_device || factory.last_desc != desc) return 25;
        if (factory.other_calls != 0) return 26;
        result = hook_create_hwnd(&factory, sentinel_device, sentinel_window, desc, fullscreen, sentinel_restrict, &created);
        if (result != 0 || created != &g_fake_chain) return 27;
        if (memcmp(desc, desc_before, sizeof desc) != 0) return 28;
        if (memcmp(fullscreen, fullscreen_before, sizeof fullscreen) != 0) return 29;
        if (factory.last_window != sentinel_window || factory.last_fullscreen != fullscreen) return 30;
        if (factory.last_restrict != sentinel_restrict) return 31;
        for (index = 0; index < 32; index++) frame[index] = (unsigned char)(index ^ 0x5A);
        memcpy(frame_before, frame, sizeof frame);
        result = hook_present1(&g_fake_chain, 0, DXGI_PRESENT_ALLOW_TEARING_FLAG | DXGI_PRESENT_DO_NOT_WAIT_FLAG | 0x4u, frame);
        if (result != (HRESULT)0x4C41544C) return 32;
        if (memcmp(frame, frame_before, sizeof frame) != 0) return 33;
        if (g_fake_chain.last_params != frame || g_fake_chain.last_sync != 1 || g_fake_chain.last_flags != 0x4u) return 34;
        result = hook_present(&g_fake_chain, 1, 0x80000208u);
        if (g_fake_chain.last_sync != 1 || g_fake_chain.last_flags != 0x80000000u) return 35;
        if (g_fake_chain.payload != 0x00C0FFEE) return 36;
        chain_slots[7] = (void *)fake_chain_get_device_fail;
        g_latency_set = 0;
        result = hook_present(&g_fake_chain, 0, DXGI_PRESENT_ALLOW_TEARING_FLAG);
        if (result != (HRESULT)0x4C41544C || g_fake_chain.last_sync != 1 || g_fake_chain.last_flags != 0) return 37;
        if (device.latency_calls != 1 || g_fake_chain.payload != 0x00C0FFEE) return 38;
        if (chain_slots[9] != (void *)fake_chain_other || chain_slots[12] != (void *)fake_chain_other) return 39;
        if (chain_slots[13] != (void *)fake_chain_other || chain_slots[15] != (void *)fake_chain_other) return 40;
        if (factory_slots[11] != (void *)fake_factory_other || factory_slots[14] != (void *)fake_factory_other) return 41;
        if (factory_slots[16] != (void *)fake_factory_other) return 42;
    }
    return 0;
}
#endif
