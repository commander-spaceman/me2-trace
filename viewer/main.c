#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define PIPE_NAME "\\\\.\\pipe\\me2-trace"
#define BUF_SIZE   4096

static FILE *g_log = NULL;

static void print_line(const char *line) {
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    WORD def = 7;

    /* Parse: {"t":12345,"e":"open","f":"filename.pcc"} */
    unsigned long tick = 0;
    char event[16] = "";
    char file[256] = "";

    const char *tp = strstr(line, "\"t\":");
    const char *ep = strstr(line, "\"e\":\"");
    const char *fp = strstr(line, "\"f\":\"");

    if (tp) tick = strtoul(tp + 4, NULL, 10);
    if (ep) {
        const char *start = ep + 5;
        const char *end = strchr(start, '"');
        if (end) {
            int len = (int)(end - start);
            if (len > 15) len = 15;
            memcpy(event, start, len);
            event[len] = '\0';
        }
    }
    if (fp) {
        const char *start = fp + 5;
        const char *end = strchr(start, '"');
        if (end) {
            int len = (int)(end - start);
            if (len > 250) len = 250;
            memcpy(file, start, len);
            file[len] = '\0';
        }
    }

    /* Format timestamp: mm:ss.ms */
    unsigned int sec  = tick / 1000;
    unsigned int ms   = tick % 1000;
    unsigned int min  = sec / 60;
    unsigned int sec2 = sec % 60;

    char ts[16];
    snprintf(ts, sizeof(ts), "%02u:%02u.%03u", min, sec2, ms);

    /* Console output with colors */
    if (strcmp(event, "open") == 0) {
        SetConsoleTextAttribute(h, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
        printf("[%s] %s\n", ts, file);
        SetConsoleTextAttribute(h, def);
        if (g_log) fprintf(g_log, "[%s] %s\n", ts, file);
    } else if (strcmp(event, "status") == 0) {
        SetConsoleTextAttribute(h, FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_INTENSITY);
        printf("[%s] %s\n", ts, file);
        SetConsoleTextAttribute(h, def);
        if (g_log) fprintf(g_log, "[%s] %s\n", ts, file);
    } else {
        printf("[%s] %s\n", ts, line);
        if (g_log) fprintf(g_log, "[%s] %s\n", ts, line);
    }

    if (g_log) fflush(g_log);
}

int main(void) {
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

    printf("[me2-trace] Viewer\n");
    printf("[me2-trace] Log: %s\n", logPath);
    printf("[me2-trace] Waiting for game (60s timeout)...\n");
    printf("---\n");

    HANDLE pipe = INVALID_HANDLE_VALUE;
    int retries = 0;
    while (pipe == INVALID_HANDLE_VALUE && retries < 120) {
        pipe = CreateFileA(PIPE_NAME, GENERIC_READ, 0, NULL,
                           OPEN_EXISTING, 0, NULL);
        if (pipe == INVALID_HANDLE_VALUE) {
            DWORD err = GetLastError();
            if (err != ERROR_PIPE_BUSY && err != ERROR_FILE_NOT_FOUND) {
                fprintf(stderr, "[me2-trace] Pipe error: %lu\n", err);
                return 1;
            }
            if (!WaitNamedPipeA(PIPE_NAME, 5000)) Sleep(500);
            retries++;
        }
    }
    if (pipe == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "[me2-trace] Pipe not available after 60s\n");
        return 1;
    }

    printf("[me2-trace] Connected\n");
    printf("---\n");

    char buf[BUF_SIZE], line[BUF_SIZE];
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
    if (li > 0) { line[li] = '\0'; print_line(line); }

    CloseHandle(pipe);
    if (g_log) { fprintf(g_log, "---\n[me2-trace] Session ended\n"); fclose(g_log); }
    return 0;
}
