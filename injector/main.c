#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>

#define GAME_EXE "ME2Game.exe"
#define HOOK_DLL  "hook.dll"

static void fatal(const char *msg) {
    fprintf(stderr, "[me2-trace] ERROR: %s (code %lu)\n", msg, GetLastError());
    exit(1);
}

static DWORD find_game_pid(void) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;

    PROCESSENTRY32W pe = {0};
    pe.dwSize = sizeof(pe);
    DWORD pid = 0;

    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, L"ME2Game.exe") == 0) {
                pid = pe.th32ProcessID;
                break;
            }
        } while (Process32NextW(snap, &pe));
    }

    CloseHandle(snap);
    return pid;
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

    size_t dirLen = strlen(dllPath);
    size_t remaining = sizeof(dllPath) - dirLen;
    if (remaining <= strlen(HOOK_DLL))
        fatal("injector directory path too long for DLL name");
    strcat(dllPath, HOOK_DLL);

    printf("[me2-trace] Injector starting (attach mode)\n");
    printf("[me2-trace] DLL path: %s\n", dllPath);

    /* Find ME2Game.exe already running */
    printf("[me2-trace] Looking for %s...\n", GAME_EXE);
    DWORD pid = 0;
    int attempts = 0;
    while (!pid && attempts < 60) {
        pid = find_game_pid();
        if (!pid) {
            printf("."); fflush(stdout);
            Sleep(1000);
            attempts++;
        }
    }
    printf("\n");

    if (!pid) {
        fprintf(stderr, "[me2-trace] ERROR: %s not found after 60s. "
                "Start the game first.\n", GAME_EXE);
        return 1;
    }

    printf("[me2-trace] Found %s PID: %lu\n", GAME_EXE, pid);

    HANDLE hProcess = OpenProcess(
        PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
        PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
        FALSE, pid);
    if (!hProcess) fatal("OpenProcess — run as Administrator");

    /* Allocate memory for DLL path in the target process */
    size_t pathLen = strlen(dllPath) + 1;
    LPVOID remoteMem = VirtualAllocEx(
        hProcess, NULL, pathLen,
        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remoteMem) {
        CloseHandle(hProcess);
        fatal("VirtualAllocEx");
    }

    if (!WriteProcessMemory(hProcess, remoteMem, dllPath, pathLen, NULL)) {
        VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        fatal("WriteProcessMemory");
    }

    /* Create remote thread to call LoadLibraryA(our DLL) */
    HANDLE remoteThread = CreateRemoteThread(
        hProcess, NULL, 0,
        (LPTHREAD_START_ROUTINE)GetProcAddress(
            GetModuleHandleA("kernel32.dll"), "LoadLibraryA"),
        remoteMem, 0, NULL);
    if (!remoteThread) {
        VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        fatal("CreateRemoteThread");
    }

    DWORD waitResult = WaitForSingleObject(remoteThread, 30000);
    if (waitResult == WAIT_TIMEOUT) {
        fprintf(stderr, "[me2-trace] ERROR: LoadLibrary timed out\n");
        TerminateThread(remoteThread, 0);
        CloseHandle(remoteThread);
        VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return 1;
    }
    if (waitResult != WAIT_OBJECT_0) {
        CloseHandle(remoteThread);
        VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        fatal("WaitForSingleObject failed");
    }

    DWORD dllBase = 0;
    GetExitCodeThread(remoteThread, &dllBase);
    CloseHandle(remoteThread);
    VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);

    if (dllBase == 0) {
        fprintf(stderr, "[me2-trace] WARNING: LoadLibrary returned NULL "
                "(DLL may have failed to load)\n");
        CloseHandle(hProcess);
        return 1;
    }
    printf("[me2-trace] DLL loaded at 0x%lX\n", dllBase);

    /* Load DLL in our own process to resolve InitPipe's RVA */
    HMODULE localDll = LoadLibraryA(dllPath);
    if (localDll) {
        FARPROC localInitPipe = GetProcAddress(localDll, "InitPipe@4");
        if (!localInitPipe)
            localInitPipe = GetProcAddress(localDll, "_InitPipe@4");
        if (!localInitPipe)
            localInitPipe = GetProcAddress(localDll, MAKEINTRESOURCEA(1));

        if (localInitPipe) {
            uintptr_t rva = (uintptr_t)localInitPipe - (uintptr_t)localDll;
            LPTHREAD_START_ROUTINE remoteInitPipe =
                (LPTHREAD_START_ROUTINE)((uintptr_t)dllBase + rva);

            printf("[me2-trace] InitPipe RVA: 0x%lX, remote: 0x%p\n",
                   (unsigned long)rva, (void *)remoteInitPipe);

            HANDLE initThread = CreateRemoteThread(
                hProcess, NULL, 0, remoteInitPipe, NULL, 0, NULL);
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

    printf("[me2-trace] Injection complete. Game is running.\n");
    CloseHandle(hProcess);
    return 0;
}
