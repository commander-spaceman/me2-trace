#include "pipe.h"
#include <stdio.h>
#include <string.h>

static HANDLE g_pipe = INVALID_HANDLE_VALUE;
static HANDLE g_pipe_thread = NULL;
static CRITICAL_SECTION g_pipe_lock;

static DWORD WINAPI pipe_server_thread(LPVOID param) {
    (void)param;

    g_pipe = CreateNamedPipeA(
        PIPE_NAME,
        PIPE_ACCESS_OUTBOUND,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
        1,                  /* max instances */
        PIPE_BUF_SIZE,      /* out buffer size */
        PIPE_BUF_SIZE,      /* in buffer size */
        0,                  /* default timeout */
        NULL                /* default security */
    );

    if (g_pipe == INVALID_HANDLE_VALUE) {
        OutputDebugStringA("[me2-trace] CreateNamedPipe failed");
        return 1;
    }

    OutputDebugStringA("[me2-trace] Pipe server waiting for viewer...");

    if (!ConnectNamedPipe(g_pipe, NULL) &&
        GetLastError() != ERROR_PIPE_CONNECTED) {
        DWORD err = GetLastError();
        if (err == ERROR_NO_DATA || err == ERROR_PIPE_NOT_CONNECTED) {
            CloseHandle(g_pipe);
            g_pipe = INVALID_HANDLE_VALUE;
            return 0;
        }
        OutputDebugStringA("[me2-trace] ConnectNamedPipe failed");
        CloseHandle(g_pipe);
        g_pipe = INVALID_HANDLE_VALUE;
        return 1;
    }

    OutputDebugStringA("[me2-trace] Viewer connected to pipe");

    pipe_write("{\"type\":\"status\",\"msg\":\"me2-trace DLL active\"}\n");

    return 0;
}

int pipe_init(void) {
    InitializeCriticalSection(&g_pipe_lock);
    g_pipe_thread = CreateThread(
        NULL, 0, pipe_server_thread, NULL, 0, NULL);
    return g_pipe_thread ? 0 : 1;
}

void pipe_shutdown(void) {
    EnterCriticalSection(&g_pipe_lock);
    if (g_pipe != INVALID_HANDLE_VALUE) {
        FlushFileBuffers(g_pipe);
        DisconnectNamedPipe(g_pipe);
        CloseHandle(g_pipe);
        g_pipe = INVALID_HANDLE_VALUE;
    }
    LeaveCriticalSection(&g_pipe_lock);

    if (g_pipe_thread) {
        WaitForSingleObject(g_pipe_thread, INFINITE);
        CloseHandle(g_pipe_thread);
        g_pipe_thread = NULL;
    }

    DeleteCriticalSection(&g_pipe_lock);
}

void pipe_write(const char *msg) {
    HANDLE pipe_snapshot;

    EnterCriticalSection(&g_pipe_lock);
    pipe_snapshot = g_pipe;
    LeaveCriticalSection(&g_pipe_lock);

    if (pipe_snapshot == INVALID_HANDLE_VALUE) return;

    DWORD written = 0;
    DWORD len = (DWORD)strlen(msg);

    if (!WriteFile(pipe_snapshot, msg, len, &written, NULL) ||
        written != len) {
        OutputDebugStringA("[me2-trace] pipe_write failed");

        EnterCriticalSection(&g_pipe_lock);
        /* Only close if another thread hasn't already done it */
        if (g_pipe != INVALID_HANDLE_VALUE &&
            g_pipe == pipe_snapshot) {
            DisconnectNamedPipe(g_pipe);
            CloseHandle(g_pipe);
            g_pipe = INVALID_HANDLE_VALUE;
        }
        LeaveCriticalSection(&g_pipe_lock);
    }
}
