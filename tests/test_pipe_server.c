#include <windows.h>
#include <stdio.h>

#define PIPE_NAME "\\\\.\\pipe\\me2-trace-test"

int main(void) {
    HANDLE pipe = CreateNamedPipeA(
        PIPE_NAME,
        PIPE_ACCESS_OUTBOUND,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
        1, 4096, 4096, 0, NULL);

    if (pipe == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "FAIL: CreateNamedPipe (code %lu)\n", GetLastError());
        return 1;
    }
    printf("PASS: CreateNamedPipe succeeded (handle %p)\n", (void *)pipe);

    /* Write a message without connecting (tests pipe_write path) */
    DWORD written;
    const char *msg = "test\n";
    DWORD len = (DWORD)strlen(msg);
    /* Note: WriteFile to an unconnected pipe may fail — that's expected */
    BOOL ok = WriteFile(pipe, msg, len, &written, NULL);
    printf("WriteFile result: %d (written=%lu, err=%lu)\n",
           ok, written, ok ? 0 : GetLastError());

    CloseHandle(pipe);
    printf("PASS: pipe test complete\n");
    return 0;
}
