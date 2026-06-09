#include <windows.h>
#include "pipe.h"

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
    (void)instance;
    (void)reserved;

    switch (reason) {
    case DLL_PROCESS_ATTACH:
        OutputDebugStringA("[me2-trace] DLL loaded");
        pipe_init();
        break;
    case DLL_PROCESS_DETACH:
        pipe_shutdown();
        OutputDebugStringA("[me2-trace] DLL unloaded");
        break;
    default:
        break;
    }
    return TRUE;
}
