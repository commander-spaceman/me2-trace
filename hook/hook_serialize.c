#include "hook_serialize.h"
#include "pattern.h"
#include "pipe.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "../vendor/minhook/include/MinHook.h"

/*
 * ULinkerLoad::Serialize(FArchive& Ar)
 *
 * This is the heart of UE3 package loading. The function deserializes export
 * data from a linker archive. Hooking it lets us see:
 *   - which export index is being loaded
 *   - the export class name
 *   - the number of bytes being deserialized
 *   - if/when it crashes (the last logged export is the suspect)
 *
 * The function is NOT exported by name — it exists as a mangled C++ symbol
 * inside ME2Game.exe. We locate it via pattern scanning.
 *
 * ── Finding the pattern ──────────────────────────────────────────
 *
 * 1. Open ME2Game.exe in IDA Pro / Ghidra / x64dbg.
 * 2. Search for the string "ULinkerLoad" in the .rdata section to find the
 *    class name. Cross-reference to its vtable and constructors.
 * 3. Look for a function that:
 *      a) Takes (this, FArchive&) as parameters
 *      b) Contains calls to FArchive::Serialize (byte-level reads)
 *      c) Is called from ULinkerLoad::Tick or the linker load loop
 * 4. Extract ~16 bytes of unique assembly near the function start.
 *
 * Example placeholder (the actual signature will differ):
 *   Pattern: "55 8B EC 83 EC ?? 53 56 8B F1 8B 45 08"
 *   This is a typical x86 function prologue + first few instructions.
 *
 * ── Architecture note ────────────────────────────────────────────
 *
 * ME2Game.exe is a 32-bit (x86) process. The calling convention for
 * C++ member functions in MSVC x86 is __thiscall: 'this' is passed in
 * ECX, arguments on the stack. Our detour must preserve ECX to access
 * the ULinkerLoad instance.
 */

/*
 * Serialize patterns – fill these from your ME2Game.exe analysis.
 * Set PATTERN_FOUND to 1 after you've verified a pattern.
 */
#define PATTERN_FOUND 0

#if PATTERN_FOUND
/* Replace with your actual pattern once found */
#define PTRN_SERIALIZE  "55 8B EC 83 EC ?? 53 56 8B F1"
#else
/* Dummy — will not match, hook is skipped gracefully */
#define PTRN_SERIALIZE  "DE AD BE EF DE AD BE EF DE AD BE EF"
#endif

/* Original function pointer (__thiscall, first arg is 'this' in ECX).
 * We store the raw function address and cast at call time. */
static void *real_serialize_addr = NULL;

/*
 * ── Detour: ULinkerLoad::Serialize ─────────────────────────────
 *
 * For x86 __thiscall, we need inline assembly or __attribute__((thiscall))
 * to access 'this'. In GCC/MinGW, __thiscall__ is supported.
 *
 * The function signature is roughly:
 *   void __thiscall Serialize(ULinkerLoad *this, FArchive *Ar);
 */

#if defined(__GNUC__) || defined(__clang__)
#define THISCALL __attribute__((thiscall))
#else
#define THISCALL __thiscall
#endif

/* Declare the original as a function pointer we can call */
typedef void (THISCALL *serialize_fn)(void *self, void *ar);

static void THISCALL detour_serialize(void *self, void *ar) {
    /* Call the original first */
    if (real_serialize_addr) {
        ((serialize_fn)real_serialize_addr)(self, ar);
    }

    /*
     * TODO: Extract useful info from self (ULinkerLoad*) and ar (FArchive*):
     *
     * ULinkerLoad layout (approximate, ME2-specific offsets):
     *   +0x00: UObject base
     *   +0x??: TArray<FObjectExport> ExportMap
     *   +0x??: INT ExportIndex (current export being loaded)
     *   +0x??: FName LinkerName (package name)
     *
     * FArchive layout:
     *   +0x00: VTable
     *   +0x08: TotalSize / Pos
     *   +0x30: ArIsLoading / ArIsSaving (BYTE flags)
     *
     * To get the export class name:
     *   FObjectExport *exp = &Linker->ExportMap(Linker->ExportIndex);
     *   FName className = exp->ClassIndex is a name table index;
     *   const wchar_t *name = Linker->NameMap[className].GetName();
     *
     * These offsets need to be verified in IDA/Ghidra for your specific
     * ME2Game.exe build.
     */
}

/* ── Public ───────────────────────────────────────────────────── */

int hook_serialize_init(void) {
    uintptr_t off = pattern_scan(L"ME2Game.exe", PTRN_SERIALIZE);
    if (off == 0) {
        OutputDebugStringA("[me2-trace] Serialize pattern not found "
                          "(need to extract from ME2Game.exe)");
        return 0; /* Not an error — patterns are not filled in yet */
    }

    HMODULE mod = GetModuleHandleW(L"ME2Game.exe");
    if (!mod) return 1;

    uintptr_t target = (uintptr_t)mod + off;
    MH_STATUS status;

    status = MH_CreateHook((LPVOID)target, detour_serialize, &real_serialize_addr);
    if (status != MH_OK) {
        char buf[128];
        snprintf(buf, sizeof(buf),
                "[me2-trace] MH_CreateHook(Serialize): %s",
                MH_StatusToString(status));
        OutputDebugStringA(buf);
        return 1;
    }

    status = MH_EnableHook((LPVOID)target);
    if (status != MH_OK) {
        char buf[128];
        snprintf(buf, sizeof(buf),
                "[me2-trace] MH_EnableHook(Serialize): %s",
                MH_StatusToString(status));
        OutputDebugStringA(buf);
        return 1;
    }

    char buf[128];
    snprintf(buf, sizeof(buf),
            "[me2-trace] Serialize hook at ME2Game.exe+0x%lX", off);
    OutputDebugStringA(buf);
    pipe_write("{\"type\":\"status\",\"msg\":\"Serialize hook active\"}\n");
    return 0;
}

void hook_serialize_shutdown(void) {
    if (real_serialize_addr) {
        MH_DisableHook(MH_ALL_HOOKS);
        real_serialize_addr = NULL;
    }
    OutputDebugStringA("[me2-trace] Serialize hook removed");
}
