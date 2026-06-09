#include "../hook/pattern.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>

/* A fixed byte array in .rdata — guaranteed to be contiguous */
static const unsigned char test_bytes[] = {
    0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xBA, 0xBE,
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08
};

int main(void) {
    wchar_t selfPath[MAX_PATH];
    GetModuleFileNameW(NULL, selfPath, MAX_PATH);

    wchar_t *fn = selfPath;
    for (wchar_t *p = selfPath; *p; p++)
        if (*p == L'\\' || *p == L'/') fn = p + 1;

    HMODULE mod = GetModuleHandleW(fn);
    if (!mod) mod = GetModuleHandleW(NULL);

    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)mod;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        fprintf(stderr, "FAIL: invalid DOS signature\n");
        return 1;
    }
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)((uintptr_t)mod + dos->e_lfanew);
    printf("Module: %ls, base=%p, size=0x%lX\n",
           fn, (void *)mod, (unsigned long)nt->OptionalHeader.SizeOfImage);
    printf("test_bytes at: %p\n", (void *)test_bytes);

    /* Scan for test_bytes in self */
    const char *pat = "DE AD BE EF CA FE BA BE 01 02 03 04 05 06 07 08";
    printf("Pattern: %s\n", pat);

    uintptr_t off = pattern_scan(fn, pat);
    if (off == 0) {
        /* Try full path instead */
        off = pattern_scan(selfPath, pat);
    }

    if (off == 0) {
        fprintf(stderr, "FAIL: pattern not found in self\n");
        fprintf(stderr, "  expected bytes at offset 0x%lX\n",
                (unsigned long)((uintptr_t)test_bytes - (uintptr_t)mod));
        return 1;
    }

    const unsigned char *found =
        (const unsigned char *)((uintptr_t)mod + off);
    printf("PASS: pattern found at offset 0x%lX\n", (unsigned long)off);
    printf("  bytes: %02X %02X %02X %02X ...\n",
           found[0], found[1], found[2], found[3]);

    /* Test 2: bogus pattern */
    off = pattern_scan(fn, "DE AD BE EF DE AD BE EF DE AD BE EF");
    if (off != 0) {
        fprintf(stderr, "FAIL: bogus pattern should not match\n");
        return 1;
    }
    printf("PASS: bogus pattern correctly returns 0\n");

    /* Test 3: wildcard */
    off = pattern_scan(fn,
            "DE AD BE EF CA FE ?? BE 01 02 03 04 05 06 07 08");
    if (off == 0) {
        fprintf(stderr, "FAIL: wildcard pattern not found\n");
        return 1;
    }
    printf("PASS: wildcard pattern matched\n");

    printf("All pattern tests passed\n");
    return 0;
}
