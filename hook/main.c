#include <windows.h>
#include "pipe.h"
#include "hook_files.h"
#include "hook_serialize.h"

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
    (void)instance;
    (void)reserved;

    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        OutputDebugStringA("[me2-trace] DLL loaded");
    } else if (reason == DLL_PROCESS_DETACH) {
        /*
         * When reserved != NULL the process is terminating — all
         * other threads have been killed.  It is safe to tear down
         * hooks without worrying about in-flight detours.
         *
         * When reserved == NULL (FreeLibrary), there may still be
         * threads executing inside our detours.  We keep hooks
         * alive to avoid use-after-free on trampoline code.
         */
        if (reserved != NULL) {
            pipe_shutdown();
            hook_serialize_shutdown();
            hook_files_shutdown();
        }
        OutputDebugStringA("[me2-trace] DLL unloaded");
    }
    return TRUE;
}

/* Called by injector via CreateRemoteThread after LoadLibrary completes.
 * Safe: loader lock is released, threads and hooks are allowed. */
__declspec(dllexport)
DWORD WINAPI InitPipe(LPVOID param) {
    (void)param;
    Sleep(100);
    OutputDebugStringA("[me2-trace] InitPipe: starting...");
    pipe_init();
    pipe_wait_connected();  /* ensure viewer is ready before init */
    pipe_write("{\"type\":\"rtti\",\"msg\":\"InitPipe: pipe connected, starting hooks\"}\n");
    hook_files_init();
    hook_serialize_init();
    OutputDebugStringA("[me2-trace] InitPipe: done");
    return 0;
}
