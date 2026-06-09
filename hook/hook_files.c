#include "hook_files.h"
#include "pipe.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "../vendor/minhook/include/MinHook.h"

/* Original function pointers */
static HANDLE (WINAPI *real_CreateFileW)(
    LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES,
    DWORD, DWORD, HANDLE) = CreateFileW;

static BOOL (WINAPI *real_ReadFile)(
    HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED) = ReadFile;

/* Check if a file extension is game-relevant */
static int is_game_ext(const wchar_t *path) {
    const wchar_t *dot = NULL;
    const wchar_t *p = path;
    while (*p) {
        if (*p == L'.') dot = p;
        if (*p == L'\\' || *p == L'/') dot = NULL;
        p++;
    }
    if (!dot) return 0;

    /* Case-insensitive extension check */
    const wchar_t *ext = dot; /* includes the dot */
    int len = (int)(p - ext);
    if (len != 4) return 0; /* .xxx = 4 chars */

    wchar_t lower[5];
    for (int i = 0; i < 4 && ext[i]; i++)
        lower[i] = (ext[i] >= L'A' && ext[i] <= L'Z') ? ext[i] + 32 : ext[i];
    lower[4] = L'\0';

    return (wcscmp(lower, L".pcc") == 0 ||
            wcscmp(lower, L".tlk") == 0 ||
            wcscmp(lower, L".upk") == 0 ||
            wcscmp(lower, L".sfm") == 0 ||
            wcscmp(lower, L".u")   == 0);
}

/* Format a wide path as JSON string in the pipe message */
static void make_json_path(const wchar_t *wpath, char *out, int outlen) {
    int pos = 0;
    out[pos++] = '"';
    while (*wpath && pos < outlen - 2) {
        wchar_t c = *wpath++;
        if (c == L'\\') { out[pos++] = '\\'; out[pos++] = '\\'; }
        else if (c == L'"')  { out[pos++] = '\\'; out[pos++] = '"'; }
        else if (c < 0x80)   { out[pos++] = (char)c; }
        else { out[pos++] = '?'; } /* non-ASCII fallback */
    }
    out[pos++] = '"';
    out[pos] = '\0';
}

/* ── Detour: CreateFileW ──────────────────────────────────────── */

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

    if (result != INVALID_HANDLE_VALUE && lpFileName && is_game_ext(lpFileName)) {
        char jsonPath[1024];
        make_json_path(lpFileName, jsonPath, sizeof(jsonPath));

        char msg[2048];
        int n = snprintf(msg, sizeof(msg),
                "{\"type\":\"file\",\"op\":\"create\",\"path\":%s}\n", jsonPath);
        if (n > 0 && n < (int)sizeof(msg)) pipe_write(msg);
    }

    return result;
}

/* ── Detour: ReadFile ─────────────────────────────────────────── */

static BOOL WINAPI detour_ReadFile(
    HANDLE hFile,
    LPVOID lpBuffer,
    DWORD nNumberOfBytesToRead,
    LPDWORD lpNumberOfBytesRead,
    LPOVERLAPPED lpOverlapped)
{
    BOOL result = real_ReadFile(hFile, lpBuffer, nNumberOfBytesToRead,
                                lpNumberOfBytesRead, lpOverlapped);

    /* Log reads over 512 bytes (likely content, not tiny header reads) */
    if (result && nNumberOfBytesToRead > 512) {
        char msg[256];
        int n = snprintf(msg, sizeof(msg),
                "{\"type\":\"file\",\"op\":\"read\",\"size\":%lu}\n",
                nNumberOfBytesToRead);
        if (n > 0 && n < (int)sizeof(msg)) pipe_write(msg);
    }

    return result;
}

/* Track whether hooks are active so shutdown is idempotent */
static int g_hooks_active = 0;

/* ── Public ───────────────────────────────────────────────────── */

int hook_files_init(void) {
    MH_STATUS status;

    if (g_hooks_active) return 0;  /* already initialized */

    status = MH_Initialize();
    if (status != MH_OK) {
        char buf[128];
        snprintf(buf, sizeof(buf),
                "[me2-trace] MH_Initialize failed: %s",
                MH_StatusToString(status));
        OutputDebugStringA(buf);
        return 1;
    }

    status = MH_CreateHookApi(
        L"kernel32.dll", "CreateFileW",
        detour_CreateFileW, (LPVOID *)&real_CreateFileW);
    if (status != MH_OK) {
        char buf[128];
        snprintf(buf, sizeof(buf),
                "[me2-trace] MH_CreateHookApi(CreateFileW): %s",
                MH_StatusToString(status));
        OutputDebugStringA(buf);
        return 1;
    }

    status = MH_CreateHookApi(
        L"kernel32.dll", "ReadFile",
        detour_ReadFile, (LPVOID *)&real_ReadFile);
    if (status != MH_OK) {
        char buf[128];
        snprintf(buf, sizeof(buf),
                "[me2-trace] MH_CreateHookApi(ReadFile): %s",
                MH_StatusToString(status));
        OutputDebugStringA(buf);
        /* Clean up CreateFileW hook created above */
        MH_RemoveHook((LPVOID)real_CreateFileW);
        real_CreateFileW = CreateFileW;
        return 1;
    }

    status = MH_EnableHook(MH_ALL_HOOKS);
    if (status != MH_OK) {
        char buf[128];
        snprintf(buf, sizeof(buf),
                "[me2-trace] MH_EnableHook: %s",
                MH_StatusToString(status));
        OutputDebugStringA(buf);
        MH_RemoveHook((LPVOID)real_CreateFileW);
        MH_RemoveHook((LPVOID)real_ReadFile);
        real_CreateFileW = CreateFileW;
        real_ReadFile = ReadFile;
        return 1;
    }

    g_hooks_active = 1;
    OutputDebugStringA("[me2-trace] File I/O hooks active");
    return 0;
}

void hook_files_shutdown(void) {
    if (!g_hooks_active) return;

    g_hooks_active = 0;

    MH_DisableHook(MH_ALL_HOOKS);
    MH_Uninitialize();
    OutputDebugStringA("[me2-trace] File I/O hooks removed");
}
