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

    char dllPath[MAX_PATH];
    DWORD exePathLen = GetModuleFileNameA(NULL, dllPath, MAX_PATH);
    if (exePathLen == 0 || exePathLen >= MAX_PATH)
        fatal("cannot determine injector path");
    char *lastSep = strrchr(dllPath, '\\');
    if (!lastSep) fatal("cannot determine injector directory");
    *(lastSep + 1) = '\0';

    /* Prevent buffer overflow when appending HOOK_DLL */
    size_t dirLen = strlen(dllPath);
    size_t remaining = sizeof(dllPath) - dirLen;
    if (remaining <= strlen(HOOK_DLL))
        fatal("injector directory path too long for DLL name");
    strcat(dllPath, HOOK_DLL);

    printf("[me2-trace] Injector starting\n");
    printf("[me2-trace] DLL path: %s\n", dllPath);
    printf("[me2-trace] Target: %s\n", GAME_EXE);

    /* Launch ME2Game.exe suspended */
    STARTUPINFOA si = {0};
    PROCESS_INFORMATION pi = {0};
    si.cb = sizeof(si);

    if (!CreateProcessA(
            NULL, (LPSTR)GAME_EXE,
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

    DWORD waitResult = WaitForSingleObject(remoteThread, 15000);
    if (waitResult == WAIT_TIMEOUT) {
        fprintf(stderr, "[me2-trace] ERROR: LoadLibrary timed out\n");
        TerminateThread(remoteThread, 0);
        CloseHandle(remoteThread);
        VirtualFreeEx(pi.hProcess, remoteMem, 0, MEM_RELEASE);
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return 1;
    }
    if (waitResult != WAIT_OBJECT_0)
        fatal("WaitForSingleObject failed");

    DWORD dllBase = 0;
    GetExitCodeThread(remoteThread, &dllBase);
    if (dllBase == 0) {
        fprintf(stderr, "[me2-trace] WARNING: LoadLibrary returned NULL "
                "(DLL may have failed to load)\n");
    } else {
        printf("[me2-trace] DLL loaded at 0x%lX\n", dllBase);
    }

    CloseHandle(remoteThread);
    VirtualFreeEx(pi.hProcess, remoteMem, 0, MEM_RELEASE);

    /* Load DLL in our own process to resolve InitPipe's RVA */
    HMODULE localDll = LoadLibraryA(dllPath);
    if (localDll) {
        FARPROC localInitPipe = GetProcAddress(localDll, "InitPipe");
        if (localInitPipe) {
            uintptr_t rva = (uintptr_t)localInitPipe - (uintptr_t)localDll;
            LPTHREAD_START_ROUTINE remoteInitPipe =
                (LPTHREAD_START_ROUTINE)((uintptr_t)dllBase + rva);

            printf("[me2-trace] InitPipe RVA: 0x%lX, remote: 0x%p\n",
                   (unsigned long)rva, (void *)remoteInitPipe);

            HANDLE initThread = CreateRemoteThread(
                pi.hProcess, NULL, 0, remoteInitPipe, NULL, 0, NULL);
            if (initThread) {
                printf("[me2-trace] InitPipe thread started\n");
                CloseHandle(initThread);
            } else {
                fprintf(stderr, "[me2-trace] WARNING: CreateRemoteThread "
                        "for InitPipe failed (code %lu)\n", GetLastError());
            }
        }
        FreeLibrary(localDll);
    }

    /* Resume the game's main thread */
    printf("[me2-trace] Resuming game process...\n");
    ResumeThread(pi.hThread);

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    return 0;
}
