#ifndef FGO_UI_CAPTURE_API_H
#define FGO_UI_CAPTURE_API_H
#include <windows.h>
#include <stdint.h>

#define FGO_UI_CAPTURE_VERSION 2u
#define FGO_UI_CAPTURE_MAGIC 0x32495546u
#ifndef FGO_UI_CAPTURE_DIR
#define FGO_UI_CAPTURE_DIR "D:\\fgoArcade\\captures\\battle-ui-v2\\"
#endif
#ifndef FGO_UI_CAPTURE_DLL
#define FGO_UI_CAPTURE_DLL "D:\\fgoArcade\\game\\App\\fgo_ui_capture_v2.dll"
#endif
enum { FGO_UI_READY=1, FGO_UI_REQUESTED, FGO_UI_LOADED, FGO_UI_CAPTURING,
       FGO_UI_COMPLETE, FGO_UI_FAILED, FGO_UI_PARTIAL };

typedef struct {
    uint32_t magic, version, pid;
    volatile LONG request, status;
    uint32_t reserved;
    uint64_t presentation;
} FgoUiControl;

typedef struct {
    int32_t binding;
    uint32_t size, type, normalized, integer, relative, enabled;
    uint64_t attribute_address, binding_address;
    int64_t attribute_length, binding_length;
    uint32_t core_buffer;
    int32_t core_stride;
    int64_t core_offset;
} FgoUiRequestedAttribute;

typedef struct {
    uint32_t version, pid;
    uint64_t presentation;
    HDC dc;
    const char *directory;
    PROC (WINAPI *resolve)(const char *);
    int (WINAPI *lookup_handle)(uint64_t, uint32_t *, uint32_t *);
} FgoUiCaptureConfig;

typedef struct {
    uint32_t mode, index_type;
    int32_t drawcount;
    const int32_t *counts, *basevertices;
    const void *const *indices;
    uint64_t game_frame;
    FgoUiRequestedAttribute requested[16];
} FgoUiCaptureDraw;

typedef struct {
    uint32_t version;
    int (WINAPI *begin)(const FgoUiCaptureConfig *);
    void (WINAPI *multi)(const FgoUiCaptureDraw *);
    int (WINAPI *finish)(HDC);
} FgoUiCaptureApi;

typedef const FgoUiCaptureApi *(WINAPI *FgoUiGetApi)(uint32_t);
#endif
