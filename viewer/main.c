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

    unsigned long tick = 0, bytes = 0;
    char event[16] = "";
    char file[256] = "";

    const char *tp = strstr(line, "\"t\":");
    const char *ep = strstr(line, "\"e\":\"");
    const char *fp = strstr(line, "\"f\":\"");
    const char *bp = strstr(line, "\"b\":");

    if (tp) tick = strtoul(tp + 4, NULL, 10);
    if (ep) {
        const char *s = ep + 5, *e = strchr(s, '"');
        if (e) { int len = (int)(e - s); if (len > 15) len = 15;
                 memcpy(event, s, len); event[len] = '\0'; }
    }
    if (fp) {
        const char *s = fp + 5, *e = strchr(s, '"');
        if (e) { int len = (int)(e - s); if (len > 250) len = 250;
                 memcpy(file, s, len); file[len] = '\0'; }
    }
    if (bp) bytes = strtoul(bp + 4, NULL, 10);

    unsigned int sec  = tick / 1000;
    unsigned int ms   = tick % 1000;
    unsigned int min  = sec / 60;
    unsigned int sec2 = sec % 60;
    char ts[16];
    snprintf(ts, sizeof(ts), "%02u:%02u.%03u", min, sec2, ms);

    char txt[512];

    if (strcmp(event, "open") == 0) {
        snprintf(txt, sizeof(txt), "[%s] OPEN  %s", ts, file);
        SetConsoleTextAttribute(h, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
    } else if (strcmp(event, "done") == 0) {
        if (bytes >= 1024 * 1024)
            snprintf(txt, sizeof(txt), "[%s] DONE  %s  (%.1f MB)",
                     ts, file, bytes / (1024.0 * 1024.0));
        else if (bytes >= 1024)
            snprintf(txt, sizeof(txt), "[%s] DONE  %s  (%lu KB)",
                     ts, file, bytes / 1024);
        else
            snprintf(txt, sizeof(txt), "[%s] DONE  %s  (%lu B)",
                     ts, file, bytes);
        SetConsoleTextAttribute(h, FOREGROUND_GREEN);
    } else {
        snprintf(txt, sizeof(txt), "[%s] %s", ts, file[0] ? file : line);
        SetConsoleTextAttribute(h, FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_INTENSITY);
    }

    printf("%s\n", txt);
    SetConsoleTextAttribute(h, def);
    if (g_log) { fprintf(g_log, "%s\n", txt); fflush(g_log); }
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
    printf("[me2-trace] Waiting for game (60s)...\n");
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
                if (g_log) fclose(g_log);
                return 1;
            }
            if (!WaitNamedPipeA(PIPE_NAME, 5000)) Sleep(500);
            retries++;
        }
    }
    if (pipe == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "[me2-trace] Pipe not available after 60s\n");
        if (g_log) fclose(g_log);
        return 1;
    }

    printf("[me2-trace] Connected\n---\n");

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
