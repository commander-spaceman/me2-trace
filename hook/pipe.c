#include "pipe.h"
#include <stdio.h>
#include <string.h>

static HANDLE g_pipe = INVALID_HANDLE_VALUE;
static HANDLE g_pipe_thread = NULL;

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

    /* Block until a client connects */
    if (!ConnectNamedPipe(g_pipe, NULL)) {
        OutputDebugStringA("[me2-trace] ConnectNamedPipe failed");
        CloseHandle(g_pipe);
        g_pipe = INVALID_HANDLE_VALUE;
        return 1;
    }

    OutputDebugStringA("[me2-trace] Viewer connected to pipe");

    /* Send a greeting so the viewer knows we're alive */
    pipe_write("{\"type\":\"status\",\"msg\":\"me2-trace DLL active\"}\n");

    return 0;
}

int pipe_init(void) {
    g_pipe_thread = CreateThread(
        NULL, 0, pipe_server_thread, NULL, 0, NULL);
    return g_pipe_thread ? 0 : 1;
}

void pipe_shutdown(void) {
    if (g_pipe != INVALID_HANDLE_VALUE) {
        FlushFileBuffers(g_pipe);
        DisconnectNamedPipe(g_pipe);
        CloseHandle(g_pipe);
        g_pipe = INVALID_HANDLE_VALUE;
    }
    if (g_pipe_thread) {
        WaitForSingleObject(g_pipe_thread, 2000);
        CloseHandle(g_pipe_thread);
        g_pipe_thread = NULL;
    }
}

void pipe_write(const char *msg) {
    if (g_pipe == INVALID_HANDLE_VALUE) return;

    DWORD written = 0;
    DWORD len = (DWORD)strlen(msg);

    if (!WriteFile(g_pipe, msg, len, &written, NULL) || written != len) {
        /* Client may have disconnected; mark pipe as dead */
        OutputDebugStringA("[me2-trace] pipe_write failed");
        DisconnectNamedPipe(g_pipe);
        CloseHandle(g_pipe);
        g_pipe = INVALID_HANDLE_VALUE;
    }
}
