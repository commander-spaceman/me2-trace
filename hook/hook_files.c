#include "hook_files.h"
#include "pipe.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "../vendor/minhook/include/MinHook.h"

/* ── Original function pointers ─────────────────────────────────── */
static HANDLE (WINAPI *real_CreateFileW)(
    LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES,
    DWORD, DWORD, HANDLE) = CreateFileW;

static BOOL (WINAPI *real_CloseHandle)(HANDLE) = CloseHandle;

static DWORD g_start_tick = 0;

/* ── Open-file tracker ────────────────────────────────────────────
 * Maps a HANDLE to its filename and accumulated read bytes.
 * Used to emit a DONE event with total bytes read on close. */
#define MAX_TRACKED 512

typedef struct {
    HANDLE handle;
    char   name[128];
    DWORD  bytes_read;
} tracked_t;

static tracked_t g_tracked[MAX_TRACKED];
static int       g_tracked_count = 0;

static void tracked_add(HANDLE h, const char *name) {
    if (g_tracked_count >= MAX_TRACKED) return;
    g_tracked[g_tracked_count].handle     = h;
    g_tracked[g_tracked_count].bytes_read = 0;
    strncpy(g_tracked[g_tracked_count].name, name, 127);
    g_tracked[g_tracked_count].name[127] = '\0';
    g_tracked_count++;
}

static tracked_t *tracked_find(HANDLE h) {
    for (int i = 0; i < g_tracked_count; i++)
        if (g_tracked[i].handle == h) return &g_tracked[i];
    return NULL;
}

static void tracked_remove(HANDLE h) {
    for (int i = 0; i < g_tracked_count; i++) {
        if (g_tracked[i].handle == h) {
            g_tracked[i] = g_tracked[--g_tracked_count];
            return;
        }
    }
}

/* Set of filenames already seen (to dedup OPEN events) */
static int seen_already(const char *name) {
    for (int i = 0; i < g_tracked_count; i++)
        if (strcmp(g_tracked[i].name, name) == 0) return 1;
    return 0;
}

/* ── Helpers ────────────────────────────────────────────────────── */

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

static void get_filename_ascii(const wchar_t *wpath, char *out, int outlen) {
    const wchar_t *name = wpath;
    for (const wchar_t *p = wpath; *p; p++)
        if (*p == L'\\' || *p == L'/') name = p + 1;
    int i = 0;
    while (name[i] && i < outlen - 1) {
        out[i] = (char)(name[i] < 0x80 ? name[i] : '?');
        i++;
    }
    out[i] = '\0';
}

static void send_msg(const char *event, const char *file, DWORD extra) {
    DWORD tick = GetTickCount() - g_start_tick;
    char msg[512];
    int n;
    if (extra > 0)
        n = snprintf(msg, sizeof(msg),
                "{\"t\":%lu,\"e\":\"%s\",\"f\":\"%s\",\"b\":%lu}\n",
                tick, event, file, extra);
    else
        n = snprintf(msg, sizeof(msg),
                "{\"t\":%lu,\"e\":\"%s\",\"f\":\"%s\"}\n",
                tick, event, file);
    if (n > 0 && n < (int)sizeof(msg)) pipe_write(msg);
}

/* ── Detour: CreateFileW ────────────────────────────────────────── */

static HANDLE WINAPI detour_CreateFileW(
    LPCWSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode,
    LPSECURITY_ATTRIBUTES lpSA, DWORD dwCreation, DWORD dwFlags,
    HANDLE hTemplate)
{
    HANDLE result = real_CreateFileW(lpFileName, dwDesiredAccess,
        dwShareMode, lpSA, dwCreation, dwFlags, hTemplate);

    if (result != INVALID_HANDLE_VALUE && lpFileName && is_game_ext(lpFileName))
    {
        char fn[256];
        get_filename_ascii(lpFileName, fn, sizeof(fn));

        if (!seen_already(fn)) {
            send_msg("open", fn, 0);
        }
        tracked_add(result, fn);
    }

    return result;
}

/* ── Detour: CloseHandle ────────────────────────────────────────── */

static BOOL WINAPI detour_CloseHandle(HANDLE hObject) {
    tracked_t *t = tracked_find(hObject);
    if (t) {
        send_msg("done", t->name, t->bytes_read);
        tracked_remove(hObject);
    }
    return real_CloseHandle(hObject);
}

/* ── Init / Shutdown ────────────────────────────────────────────── */

static int g_hooks_active = 0;

int hook_files_init(void) {
    MH_STATUS status;
    if (g_hooks_active) return 0;

    g_start_tick = GetTickCount();
    status = MH_Initialize();
    if (status != MH_OK) { OutputDebugStringA("[me2-trace] MH_Init fail"); return 1; }

    status = MH_CreateHookApi(L"kernel32.dll", "CreateFileW",
        detour_CreateFileW, (LPVOID *)&real_CreateFileW);
    if (status != MH_OK) { OutputDebugStringA("[me2-trace] CreateFileW fail"); return 1; }

    status = MH_CreateHookApi(L"kernel32.dll", "CloseHandle",
        detour_CloseHandle, (LPVOID *)&real_CloseHandle);
    if (status != MH_OK) {
        OutputDebugStringA("[me2-trace] CloseHandle fail");
        MH_RemoveHook((LPVOID)real_CreateFileW);
        real_CreateFileW = CreateFileW;
        return 1;
    }

    status = MH_EnableHook(MH_ALL_HOOKS);
    if (status != MH_OK) {
        OutputDebugStringA("[me2-trace] Enable fail");
        MH_RemoveHook((LPVOID)real_CreateFileW);
        MH_RemoveHook((LPVOID)real_CloseHandle);
        real_CreateFileW = CreateFileW;
        real_CloseHandle = CloseHandle;
        return 1;
    }

    g_hooks_active = 1;
    OutputDebugStringA("[me2-trace] File hooks active (open + close)");
    return 0;
}

void hook_files_shutdown(void) {
    if (!g_hooks_active) return;
    g_hooks_active = 0;
    g_tracked_count = 0;
    MH_DisableHook(MH_ALL_HOOKS);
    MH_Uninitialize();
    OutputDebugStringA("[me2-trace] File hooks removed");
}
