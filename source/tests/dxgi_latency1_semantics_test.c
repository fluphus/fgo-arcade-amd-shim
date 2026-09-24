#include <stdio.h>
#include <string.h>
#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dxgi1_3.h>

extern int present_dxgi_latency1_semantics_test(void);

static int code_pointer(void *fn)
{
    MEMORY_BASIC_INFORMATION info;
    DWORD protect;
    if (!fn || !VirtualQuery(fn, &info, sizeof info)) return 0;
    protect = info.Protect & 0xFF;
    return protect == PAGE_EXECUTE || protect == PAGE_EXECUTE_READ
        || protect == PAGE_EXECUTE_READWRITE || protect == PAGE_EXECUTE_WRITECOPY;
}

static int check_slot(void *object, int index, void *expected, const char *label)
{
    void **table;
    MEMORY_BASIC_INFORMATION info;
    unsigned char *end;
    if (!object) {
        printf("FAIL %s missing object\n", label);
        return 1;
    }
    table = *(void ***)object;
    if (!table || !VirtualQuery(table, &info, sizeof info)) {
        printf("FAIL %s vtable query\n", label);
        return 1;
    }
    end = (unsigned char *)table + (size_t)(index + 1) * sizeof(void *);
    if (end > (unsigned char *)info.BaseAddress + info.RegionSize) {
        printf("FAIL %s slot %d outside committed vtable\n", label, index);
        return 1;
    }
    if (!code_pointer(table[index])) {
        printf("FAIL %s slot %d is not executable\n", label, index);
        return 1;
    }
    if (expected && table[index] != expected) {
        printf("FAIL %s slot %d mismatch hook index\n", label, index);
        return 1;
    }
    printf("ok %s slot %d\n", label, index);
    return 0;
}

static int prologue_matches(void)
{
    static const unsigned char factory_prefix[] = {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xDA, 0x4C, 0x8B, 0xC2
    };
    static const unsigned char factory2_prefix[] = {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20
    };
    HMODULE dxgi;
    unsigned char *factory;
    unsigned char *factory1;
    unsigned char *factory2;
    dxgi = LoadLibraryA("dxgi.dll");
    if (!dxgi) return 1;
    factory = (unsigned char *)GetProcAddress(dxgi, "CreateDXGIFactory");
    factory1 = (unsigned char *)GetProcAddress(dxgi, "CreateDXGIFactory1");
    factory2 = (unsigned char *)GetProcAddress(dxgi, "CreateDXGIFactory2");
    if (!factory || memcmp(factory, factory_prefix, sizeof factory_prefix) != 0) return 1;
    if (!factory1 || memcmp(factory1, factory_prefix, sizeof factory_prefix) != 0) return 1;
    if (!factory2 || memcmp(factory2, factory2_prefix, sizeof factory2_prefix) != 0) return 1;
    return 0;
}

static int live_dxgi_layout(void)
{
    IDXGIFactory *factory = NULL;
    IDXGIFactory2 *factory2 = NULL;
    ID3D11Device *device = NULL;
    ID3D11DeviceContext *context = NULL;
    IDXGISwapChain *chain = NULL;
    IDXGISwapChain1 *chain1 = NULL;
    IDXGIDevice1 *device1 = NULL;
    HWND window = NULL;
    DXGI_SWAP_CHAIN_DESC desc;
    D3D_FEATURE_LEVEL level;
    HRESULT result;
    int failed = 0;

    if (prologue_matches() != 0) {
        printf("FAIL installed prologue does not match dxgi.dll\n");
        return 1;
    }
    printf("ok factory prologue matches; hook would install without a byte mismatch\n");

    result = CreateDXGIFactory(&IID_IDXGIFactory, (void **)&factory);
    if (result < 0 || !factory) {
        printf("FAIL CreateDXGIFactory hr=0x%08lx\n", (unsigned long)result);
        return 1;
    }
    failed |= check_slot(factory, 10, (void *)factory->lpVtbl->CreateSwapChain, "IDXGIFactory CreateSwapChain");
    failed |= check_slot(factory, 15, NULL, "IDXGIFactory concrete slot 15");

    result = CreateDXGIFactory2(0, &IID_IDXGIFactory2, (void **)&factory2);
    if (result < 0 || !factory2) {
        printf("FAIL CreateDXGIFactory2 hr=0x%08lx\n", (unsigned long)result);
        if (factory) factory->lpVtbl->Release(factory);
        return 1;
    }
    failed |= check_slot(factory2, 10, (void *)factory2->lpVtbl->CreateSwapChain, "IDXGIFactory2 CreateSwapChain");
    failed |= check_slot(factory2, 15, (void *)factory2->lpVtbl->CreateSwapChainForHwnd, "IDXGIFactory2 CreateSwapChainForHwnd");
    {
        void **short_table = *(void ***)factory;
        void **long_table = *(void ***)factory2;
        if (short_table[15] != long_table[15]) {
            printf("FAIL CreateDXGIFactory slot 15 is not CreateSwapChainForHwnd\n");
            failed = 1;
        } else {
            printf("ok CreateDXGIFactory slot 15 is CreateSwapChainForHwnd\n");
        }
    }

    window = CreateWindowExW(WS_EX_NOACTIVATE, L"STATIC", L"", WS_POPUP,
                              0, 0, 64, 64, NULL, NULL, GetModuleHandleW(NULL), NULL);
    memset(&desc, 0, sizeof desc);
    desc.BufferCount = 2;
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.OutputWindow = window;
    desc.SampleDesc.Count = 1;
    desc.Windowed = TRUE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
    result = D3D11CreateDeviceAndSwapChain(
        NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0, D3D11_SDK_VERSION,
        &desc, &chain, &device, &level, &context);
    if (result < 0 || !chain || !device) {
        printf("FAIL D3D11 flip chain hr=0x%08lx\n", (unsigned long)result);
        failed = 1;
    } else {
        failed |= check_slot(chain, 7, (void *)chain->lpVtbl->GetDevice, "IDXGISwapChain GetDevice");
        failed |= check_slot(chain, 8, (void *)chain->lpVtbl->Present, "IDXGISwapChain Present");
        result = chain->lpVtbl->QueryInterface(chain, &IID_IDXGISwapChain1, (void **)&chain1);
        if (result < 0 || !chain1) {
            printf("FAIL QueryInterface IDXGISwapChain1 hr=0x%08lx\n", (unsigned long)result);
            failed = 1;
        } else {
            failed |= check_slot(chain1, 22, (void *)chain1->lpVtbl->Present1, "IDXGISwapChain1 Present1");
            if ((*(void ***)chain)[22] != chain1->lpVtbl->Present1) {
                printf("FAIL base swapchain slot 22 is not Present1\n");
                failed = 1;
            } else {
                printf("ok base swapchain slot 22 is Present1\n");
            }
        }
        result = chain->lpVtbl->GetDevice(chain, &IID_IDXGIDevice1, (void **)&device1);
        if (result < 0 || !device1) {
            printf("FAIL GetDevice IDXGIDevice1 hr=0x%08lx\n", (unsigned long)result);
            failed = 1;
        } else {
            failed |= check_slot(device1, 12, (void *)device1->lpVtbl->SetMaximumFrameLatency, "IDXGIDevice1 SetMaximumFrameLatency");
            result = device1->lpVtbl->SetMaximumFrameLatency(device1, 1);
            if (result < 0) {
                printf("FAIL SetMaximumFrameLatency(1) hr=0x%08lx\n", (unsigned long)result);
                failed = 1;
            } else {
                printf("ok throwaway SetMaximumFrameLatency(1)\n");
            }
        }
    }

    if (device1) device1->lpVtbl->Release(device1);
    if (chain1) chain1->lpVtbl->Release(chain1);
    if (chain) chain->lpVtbl->Release(chain);
    if (context) context->lpVtbl->Release(context);
    if (device) device->lpVtbl->Release(device);
    if (window) DestroyWindow(window);
    if (factory2) factory2->lpVtbl->Release(factory2);
    if (factory) factory->lpVtbl->Release(factory);
    return failed;
}

int main(void)
{
    int code = present_dxgi_latency1_semantics_test();
    if (code != 0) {
        printf("FAIL dxgi semantics code %d\n", code);
        return 1;
    }
    puts("PASS dxgi present rewrite keeps frame payload and leaves non-present slots unchanged");
    if (live_dxgi_layout() != 0) return 1;
    puts("PASS live dxgi slots match the hook and were not patched");
    return 0;
}
