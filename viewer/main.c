#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define PIPE_NAME "\\\\.\\pipe\\me2-trace"
#define BUF_SIZE   4096
#define STUCK_MS   5000   /* warn if file open > 5 sec without DONE */
#define MAX_OPEN   64     /* max concurrent tracked opens */

/* Track in-flight opens for stuck detection */
typedef struct {
    char name[256];
    DWORD tick;
} pending_t;

static pending_t g_open[MAX_OPEN];
static int       g_open_count = 0;
static FILE *    g_log = NULL;
static DWORD     g_start_tick = 0;

static void track_open(const char *file, DWORD tick) {
    (void)tick;
    if (g_open_count >= MAX_OPEN) return;
    strncpy(g_open[g_open_count].name, file, 255);
    g_open[g_open_count].name[255] = '\0';
    g_open[g_open_count].tick = GetTickCount();  /* wall-clock */
    g_open_count++;
}

static DWORD track_done(const char *file) {
    for (int i = 0; i < g_open_count; i++) {
        if (strcmp(g_open[i].name, file) == 0) {
            DWORD elapsed = GetTickCount() - g_open[i].tick;
            g_open[i] = g_open[--g_open_count];
            return elapsed;
        }
    }
    return 0;
}

static void check_stuck(void) {
    for (int i = 0; i < g_open_count; i++) {
        DWORD elapsed = GetTickCount() - g_open[i].tick;
        if (elapsed > STUCK_MS) {
            time_t now_abs = time(NULL);
            struct tm *lt = localtime(&now_abs);
            char abs_ts[24];
            strftime(abs_ts, sizeof(abs_ts), "%Y-%m-%d %H:%M:%S", lt);

            DWORD rel_tick = GetTickCount() - g_start_tick;
            unsigned int sec  = rel_tick / 1000;
            unsigned int ms   = rel_tick % 1000;
            unsigned int min  = sec / 60;
            unsigned int sec2 = sec % 60;
            char rel[16];
            snprintf(rel, sizeof(rel), "%02u:%02u.%03u", min, sec2, ms);

            char txt[512];
            snprintf(txt, sizeof(txt),
                     "[%s] [%s] STUCK? %s  (%.1fs since OPEN)",
                     abs_ts, rel, g_open[i].name, elapsed / 1000.0);

            HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
            WORD def = 7;
            SetConsoleTextAttribute(h,
                FOREGROUND_RED | FOREGROUND_INTENSITY);
            printf("%s\n", txt);
            SetConsoleTextAttribute(h, def);
            if (g_log) { fprintf(g_log, "%s\n", txt); fflush(g_log); }

            g_open[i].tick = GetTickCount();
        }
    }
}

/* Phase markers: key files -> section header */
static const char *phase_marker(const char *fn) {
    if (strstr(fn, "EntryMenu.pcc"))    return "Main Menu";
    if (strstr(fn, "BioP_Nor.pcc"))     return "Normandy SR-2";
    if (strstr(fn, "BioP_CitHub.pcc"))  return "Citadel";
    if (strstr(fn, "BioP_ProFre.pcc"))  return "Omega";
    if (strstr(fn, "BioP_TwrHub.pcc"))  return "Illium";
    if (strstr(fn, "BioP_OmgHub.pcc"))  return "Omega Hub";
    if (strstr(fn, "BioP_KroHub.pcc"))  return "Tuchanka";
    if (strstr(fn, "BioP_QuaHub.pcc"))  return "Flotilla";
    if (strstr(fn, "BioP_EndGm1.pcc"))  return "Suicide Mission";
    return NULL;
}

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
    char rel[16];
    snprintf(rel, sizeof(rel), "%02u:%02u.%03u", min, sec2, ms);

    time_t now_abs = time(NULL);
    struct tm *lt = localtime(&now_abs);
    char abs_ts[24];
    strftime(abs_ts, sizeof(abs_ts), "%Y-%m-%d %H:%M:%S", lt);

    /* Check for stuck opens */
    check_stuck();

    /* Phase marker */
    const char *phase = NULL;
    if (strcmp(event, "open") == 0 && file[0]) {
        phase = phase_marker(file);
    }

    char txt[512];

    if (phase) {
        snprintf(txt, sizeof(txt), "--- %s ---", phase);
        SetConsoleTextAttribute(h,
            FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_INTENSITY);
        printf("[%s] [%s]\n", abs_ts, rel);
        printf("%s\n", txt);
        SetConsoleTextAttribute(h, def);
        if (g_log) fprintf(g_log, "%s\n", txt);
    }

    if (strcmp(event, "open") == 0) {
        snprintf(txt, sizeof(txt), "[%s] [%s] OPEN  %s", abs_ts, rel, file);
        SetConsoleTextAttribute(h, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
        track_open(file, tick);
    } else if (strcmp(event, "done") == 0) {
        DWORD elapsed = track_done(file);
        if (elapsed > 1000)
            snprintf(txt, sizeof(txt), "[%s] [%s] DONE  %s  (%.2fs)",
                     abs_ts, rel, file, elapsed / 1000.0);
        else
            snprintf(txt, sizeof(txt), "[%s] [%s] DONE  %s  (%lu ms)",
                     abs_ts, rel, file, elapsed);
        SetConsoleTextAttribute(h, FOREGROUND_GREEN);
    } else {
        snprintf(txt, sizeof(txt), "[%s] [%s] %s", abs_ts, rel,
                 file[0] ? file : line);
        SetConsoleTextAttribute(h,
            FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_INTENSITY);
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
    g_start_tick = GetTickCount();

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
