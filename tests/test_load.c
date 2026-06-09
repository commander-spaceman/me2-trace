#include <windows.h>
#include <stdio.h>

int main(void) {
    HMODULE h = LoadLibraryA("hook.dll");
    if (!h) {
        fprintf(stderr, "FAIL: LoadLibrary returned NULL (code %lu)\n",
                GetLastError());
        return 1;
    }
    printf("PASS: hook.dll loaded at 0x%p\n", (void *)h);
    FreeLibrary(h);
    return 0;
}
