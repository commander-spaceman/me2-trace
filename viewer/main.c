#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define PIPE_NAME "\\\\.\\pipe\\me2-trace"
#define BUF_SIZE   4096

static FILE *g_log = NULL;

/* Write a line to both console (colored) and log file (plain) */
static void print_line(const char *line) {
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    WORD default_attr = 7;

    const char *prefix = "";

    if (strstr(line, "\"type\":\"file\"")) {
        SetConsoleTextAttribute(h, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
        prefix = "[FILE] ";
    } else if (strstr(line, "\"type\":\"serialize\"")) {
        SetConsoleTextAttribute(h, FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        prefix = "[SER]  ";
    } else if (strstr(line, "\"type\":\"status\"")) {
        SetConsoleTextAttribute(h, FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_INTENSITY);
        prefix = "[STAT] ";
    } else if (strstr(line, "\"type\":\"err\"")) {
        SetConsoleTextAttribute(h, FOREGROUND_RED | FOREGROUND_INTENSITY);
        prefix = "[ERR]  ";
    }

    /* Console */
    printf("%s%s\n", prefix, line);
    SetConsoleTextAttribute(h, default_attr);

    /* Log file */
    if (g_log) {
        fprintf(g_log, "%s%s\n", prefix, line);
        fflush(g_log);
    }
}

int main(void) {
    /* Create timestamped log file next to viewer */
    char logPath[MAX_PATH];
    GetModuleFileNameA(NULL, logPath, MAX_PATH);
    char *ext = strrchr(logPath, '.');
    if (ext) *ext = '\0';

    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char timebuf[32];
    strftime(timebuf, sizeof(timebuf), "_%Y%m%d_%H%M%S", tm_info);

    strcat(logPath, timebuf);
    strcat(logPath, ".log");

    g_log = fopen(logPath, "w");
    if (!g_log) {
        fprintf(stderr, "[me2-trace] WARNING: cannot create log file %s\n",
                logPath);
    }

    printf("[me2-trace] Viewer connecting to pipe...\n");
    printf("[me2-trace] Log file: %s\n", logPath);
    printf("[me2-trace] Waiting for game to start (timeout 60s)...\n");
    printf("---\n");

    /* Try to connect; the DLL may not have created the pipe yet */
    HANDLE pipe = INVALID_HANDLE_VALUE;
    int retries = 0;

    while (pipe == INVALID_HANDLE_VALUE && retries < 120) {
        pipe = CreateFileA(
            PIPE_NAME,
            GENERIC_READ,
            0,              /* no sharing */
            NULL,
            OPEN_EXISTING,
            0,
            NULL);

        if (pipe == INVALID_HANDLE_VALUE) {
            DWORD err = GetLastError();
            if (err != ERROR_PIPE_BUSY && err != ERROR_FILE_NOT_FOUND) {
                fprintf(stderr, "[me2-trace] ERROR: CreateFile pipe failed "
                        "(code %lu)\n", err);
                return 1;
            }
            /* Wait and retry */
            if (!WaitNamedPipeA(PIPE_NAME, 5000)) {
                Sleep(500);
            }
            retries++;
        }
    }

    if (pipe == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "[me2-trace] ERROR: Pipe not available after 60s\n");
        return 1;
    }

    printf("[me2-trace] Connected! Streaming events...\n");
    printf("---\n");

    /* Read loop */
    char buf[BUF_SIZE];
    char line[BUF_SIZE];
    DWORD bytesRead;
    int li = 0;

    while (ReadFile(pipe, buf, BUF_SIZE - 1, &bytesRead, NULL) && bytesRead > 0) {
        for (DWORD i = 0; i < bytesRead; i++) {
            if (buf[i] == '\n') {
                line[li] = '\0';
                if (li > 0) print_line(line);
                li = 0;
            } else if (li < (int)(BUF_SIZE - 2)) {
                line[li++] = buf[i];
            }
        }
    }

    /* Flush any partial line buffered before disconnect */
    if (li > 0) {
        line[li] = '\0';
        print_line(line);
    }

    DWORD err = GetLastError();
    if (err == ERROR_BROKEN_PIPE) {
        printf("---\n[me2-trace] Game disconnected (pipe closed).\n");
    } else {
        fprintf(stderr, "[me2-trace] ReadFile error: %lu\n", err);
    }

    CloseHandle(pipe);
    if (g_log) {
        fprintf(g_log, "---\n[me2-trace] Session ended\n");
        fclose(g_log);
    }
    return 0;
}
