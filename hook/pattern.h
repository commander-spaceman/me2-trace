#ifndef ME2_TRACE_PATTERN_H
#define ME2_TRACE_PATTERN_H

#include <windows.h>

/* Scan a module's memory for a byte pattern with ? wildcards.
 * pattern: "8B 44 24 04 ?? 89" — space-separated hex bytes, ? for wildcard.
 * Returns offset from module base, or 0 on failure. */
uintptr_t pattern_scan(const wchar_t *module_name, const char *pattern);

#endif
