#include "hook_files.h"
#include "pipe.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "../vendor/minhook/include/MinHook.h"

static HANDLE (WINAPI *real_CreateFileW)(
    LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES,
    DWORD, DWORD, HANDLE) = CreateFileW;

static DWORD g_start_tick = 0;

static int is_game_ext(const wchar_t *path) {
    const wchar_t *dot = NULL;
    const wchar_t *p = path;
    while (*p) {
        if (*p == L'.') dot = p;
        if (*p == L'\\' || *p == L'/') dot = NULL;
        p++;
    }
    if (!dot) return 0;

    int len = (int)(p - dot);
    if (len != 4) return 0;

    wchar_t lower[5];
    for (int i = 0; i < 4 && dot[i]; i++)
        lower[i] = (dot[i] >= L'A' && dot[i] <= L'Z') ? dot[i] + 32 : dot[i];
    lower[4] = L'\0';

    return (wcscmp(lower, L".pcc") == 0 ||
            wcscmp(lower, L".tlk") == 0 ||
            wcscmp(lower, L".upk") == 0 ||
            wcscmp(lower, L".sfm") == 0 ||
            wcscmp(lower, L".u")   == 0);
}

/* Extract filename (last component) from a path, converting to ASCII */
static void get_filename_ascii(const wchar_t *wpath, char *out, int outlen) {
    const wchar_t *name = wpath;
    for (const wchar_t *p = wpath; *p; p++) {
        if (*p == L'\\' || *p == L'/') name = p + 1;
    }
    int i = 0;
    while (name[i] && i < outlen - 1) {
        out[i] = (char)(name[i] < 0x80 ? name[i] : '?');
        i++;
    }
    out[i] = '\0';
}

/* Format: "tick|OPEN|filename.pcc" */
static HANDLE WINAPI detour_CreateFileW(
    LPCWSTR lpFileName,
    DWORD dwDesiredAccess,
    DWORD dwShareMode,
    LPSECURITY_ATTRIBUTES lpSecurityAttributes,
    DWORD dwCreationDisposition,
    DWORD dwFlagsAndAttributes,
    HANDLE hTemplateFile)
{
    HANDLE result = real_CreateFileW(lpFileName, dwDesiredAccess, dwShareMode,
                                     lpSecurityAttributes, dwCreationDisposition,
                                     dwFlagsAndAttributes, hTemplateFile);

    if (result != INVALID_HANDLE_VALUE && lpFileName && is_game_ext(lpFileName))
    {
        char fn[256];
        get_filename_ascii(lpFileName, fn, sizeof(fn));

        DWORD tick = GetTickCount() - g_start_tick;
        char msg[512];
        int n = snprintf(msg, sizeof(msg),
                "{\"t\":%lu,\"e\":\"open\",\"f\":\"%s\"}\n", tick, fn);
        if (n > 0 && n < (int)sizeof(msg)) pipe_write(msg);
    }

    return result;
}

static int g_hooks_active = 0;

int hook_files_init(void) {
    MH_STATUS status;

    if (g_hooks_active) return 0;

    g_start_tick = GetTickCount();

    status = MH_Initialize();
    if (status != MH_OK) {
        OutputDebugStringA("[me2-trace] MH_Initialize failed");
        return 1;
    }

    status = MH_CreateHookApi(
        L"kernel32.dll", "CreateFileW",
        detour_CreateFileW, (LPVOID *)&real_CreateFileW);
    if (status != MH_OK) {
        OutputDebugStringA("[me2-trace] CreateFileW hook failed");
        return 1;
    }

    status = MH_EnableHook(MH_ALL_HOOKS);
    if (status != MH_OK) {
        OutputDebugStringA("[me2-trace] MH_EnableHook failed");
        MH_RemoveHook((LPVOID)real_CreateFileW);
        real_CreateFileW = CreateFileW;
        return 1;
    }

    g_hooks_active = 1;
    OutputDebugStringA("[me2-trace] File hooks active");
    return 0;
}

void hook_files_shutdown(void) {
    if (!g_hooks_active) return;
    g_hooks_active = 0;
    MH_DisableHook(MH_ALL_HOOKS);
    MH_Uninitialize();
    OutputDebugStringA("[me2-trace] File hooks removed");
}
