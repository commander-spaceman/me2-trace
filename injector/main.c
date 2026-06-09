#include <windows.h>
#include <stdio.h>

#define GAME_EXE "ME2Game.exe"
#define HOOK_DLL  "hook.dll"

static void fatal(const char *msg) {
    fprintf(stderr, "[me2-trace] ERROR: %s (code %lu)\n", msg, GetLastError());
    exit(1);
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    /* Find the DLL next to the injector */
    char dllPath[MAX_PATH];
    GetModuleFileNameA(NULL, dllPath, MAX_PATH);
    char *lastSep = strrchr(dllPath, '\\');
    if (!lastSep) fatal("cannot determine injector path");
    *(lastSep + 1) = '\0';
    strcat(dllPath, HOOK_DLL);

    printf("[me2-trace] Injector starting\n");
    printf("[me2-trace] DLL path: %s\n", dllPath);
    printf("[me2-trace] Target: %s\n", GAME_EXE);

    /* Launch ME2Game.exe suspended */
    STARTUPINFOA si = {0};
    PROCESS_INFORMATION pi = {0};
    si.cb = sizeof(si);

    if (!CreateProcessA(
            NULL,        /* use command line */
            (LPSTR)GAME_EXE,
            NULL, NULL, FALSE,
            CREATE_SUSPENDED,
            NULL, NULL,
            &si, &pi)) {
        fatal("CreateProcess failed — is ME2Game.exe in PATH?");
    }

    printf("[me2-trace] Game PID: %lu\n", pi.dwProcessId);

    /* Allocate memory for DLL path in the target process */
    size_t pathLen = strlen(dllPath) + 1;
    LPVOID remoteMem = VirtualAllocEx(
        pi.hProcess, NULL, pathLen,
        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remoteMem) fatal("VirtualAllocEx");

    if (!WriteProcessMemory(pi.hProcess, remoteMem, dllPath, pathLen, NULL))
        fatal("WriteProcessMemory");

    /* Create remote thread to call LoadLibraryA(our DLL) */
    HANDLE remoteThread = CreateRemoteThread(
        pi.hProcess, NULL, 0,
        (LPTHREAD_START_ROUTINE)GetProcAddress(
            GetModuleHandleA("kernel32.dll"), "LoadLibraryA"),
        remoteMem, 0, NULL);
    if (!remoteThread) fatal("CreateRemoteThread");

    /* Wait for LoadLibrary to complete */
    WaitForSingleObject(remoteThread, INFINITE);

    DWORD exitCode = 0;
    GetExitCodeThread(remoteThread, &exitCode);
    if (exitCode == 0) {
        fprintf(stderr, "[me2-trace] WARNING: LoadLibrary returned NULL "
                "(DLL may have failed to load)\n");
    } else {
        printf("[me2-trace] DLL loaded at 0x%lX\n", exitCode);
    }

    CloseHandle(remoteThread);
    VirtualFreeEx(pi.hProcess, remoteMem, 0, MEM_RELEASE);

    /* Resume the game's main thread */
    printf("[me2-trace] Resuming game process...\n");
    ResumeThread(pi.hThread);

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    return 0;
}
