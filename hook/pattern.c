#include "pattern.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>

/* Parse one hex byte from a pattern string. Returns -1 for wildcard. */
static int parse_byte(const char **p) {
    while (**p == ' ') (*p)++;
    if (**p == '?') {
        (*p)++;
        if (**p == '?') (*p)++;
        return -1;
    }
    if (**p == '\0') return -2;
    char high = **p;
    (*p)++;
    if (**p == '\0') return -2;
    char low = **p;
    (*p)++;
    int val = 0;
    if (high >= '0' && high <= '9') val = (high - '0') << 4;
    else if (high >= 'A' && high <= 'F') val = (high - 'A' + 10) << 4;
    else if (high >= 'a' && high <= 'f') val = (high - 'a' + 10) << 4;
    else return -2;
    if (low >= '0' && low <= '9') val |= (low - '0');
    else if (low >= 'A' && low <= 'F') val |= (low - 'A' + 10);
    else if (low >= 'a' && low <= 'f') val |= (low - 'a' + 10);
    else return -2;
    return val;
}

/* Convert pattern string to byte array + mask. Caller allocates. */
static int compile_pattern(const char *pattern, unsigned char *bytes,
                           char *mask, int maxlen) {
    const char *p = pattern;
    int count = 0;
    while (count < maxlen) {
        int b = parse_byte(&p);
        if (b == -2) break; /* end */
        if (b >= 0) {
            bytes[count] = (unsigned char)b;
            mask[count] = 'x';
        } else {
            bytes[count] = 0;
            mask[count] = '?';
        }
        count++;
    }
    return count;
}

uintptr_t pattern_scan(const wchar_t *module_name, const char *pattern) {
    HMODULE mod = GetModuleHandleW(module_name);
    if (!mod) {
        OutputDebugStringA("[me2-trace] pattern_scan: module not loaded");
        return 0;
    }

    /* Get module bounds from PE headers */
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)mod;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)((uintptr_t)mod + dos->e_lfanew);
    uintptr_t base = (uintptr_t)mod;
    size_t size = nt->OptionalHeader.SizeOfImage;

    unsigned char pat_bytes[256];
    char pat_mask[256];
    int pat_len = compile_pattern(pattern, pat_bytes, pat_mask, 256);
    if (pat_len <= 0) return 0;

    /* Linear scan */
    const unsigned char *start = (const unsigned char *)base;
    const unsigned char *end = start + size - pat_len;

    for (const unsigned char *addr = start; addr < end; addr++) {
        int match = 1;
        for (int i = 0; i < pat_len; i++) {
            if (pat_mask[i] == 'x' && addr[i] != pat_bytes[i]) {
                match = 0;
                break;
            }
        }
        if (match) {
            return (uintptr_t)addr - base;
        }
    }

    char buf[256];
    snprintf(buf, sizeof(buf),
            "[me2-trace] pattern not found in %ls", module_name);
    OutputDebugStringA(buf);
    return 0;
}
