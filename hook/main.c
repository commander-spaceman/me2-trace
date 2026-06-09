#include <windows.h>
#include <stdio.h>

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
    (void)instance;
    (void)reserved;

    switch (reason) {
    case DLL_PROCESS_ATTACH:
        OutputDebugStringA("[me2-trace] DLL loaded successfully");
        break;
    case DLL_PROCESS_DETACH:
        OutputDebugStringA("[me2-trace] DLL unloaded");
        break;
    default:
        break;
    }
    return TRUE;
}
