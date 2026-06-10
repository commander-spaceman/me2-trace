/* ExtractPattern.java
 *
 * Ghidra post-analysis script: prints vtable entries of ULinkerLoad
 * and the first 32 bytes of each as an IDA-style hex pattern.
 *
 * Usage:
 *   analyzeHeadless <project> <folder> -process ME2Game.exe -postScript path/to/ExtractPattern.java
 */

import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.address.*;
import ghidra.program.model.mem.*;

public class ExtractPattern extends GhidraScript {

    @Override
    public void run() throws Exception {
        Program program = getCurrentProgram();
        Memory memory = program.getMemory();
        Listing listing = program.getListing();
        ReferenceManager refMgr = program.getReferenceManager();

        println("=== me2-trace: ULinkerLoad::Serialize Pattern Extractor ===");

        /* Step 1: Find "ULinkerLoad" or "LinkerLoad" string */
        Address strAddr = findStringContaining("LinkerLoad");
        if (strAddr == null) {
            strAddr = findStringContaining("ULinkerLoad");
        }
        if (strAddr == null) {
            println("ERROR: 'LinkerLoad' string not found");
            return;
        }
        println("String containing 'LinkerLoad' at " + strAddr);
        Data data = listing.getDefinedDataAt(strAddr);
        if (data != null) {
            println("  Value: " + data.getDefaultValueRepresentation());
        }

        /* Step 2: Find TypeDescriptor (points back to string) */
        Address tdAddr = findTypeDescriptor(strAddr, memory);
        if (tdAddr == null) {
            println("ERROR: TypeDescriptor not found");
            println("Fallback: showing refs to string...");
            printStringRefs(strAddr, refMgr);
            return;
        }
        println("TypeDescriptor at " + tdAddr);

        /* Step 3: Find references to TypeDescriptor */
        println("References to TypeDescriptor:");
        ReferenceIterator tdRefs = refMgr.getReferencesTo(tdAddr);
        while (tdRefs.hasNext() && !monitor.isCancelled()) {
            Address from = tdRefs.next().getFromAddress();
            println("  -> referenced by: " + from);
        }

        /* Step 4: Scan for function pointers near the COL that look
         * like vtable entries (point to .text section).  Print the
         * first 16 for manual inspection. */
        long near = tdAddr.getOffset();
        println("");
        println("Scanning for vtable candidates near TypeDescriptor...");

        for (long off = -256; off <= 512; off += 4) {
            try {
                Address addr = tdAddr.getNewAddress(near + off);
                long val = memory.getInt(addr) & 0xFFFFFFFFL;
                if (val == 0) continue;

                /* Convert absolute address to program address */
                long imageBase = program.getImageBase().getOffset();
                long rva = val - imageBase;
                if (rva < 0 || rva > 0x2000000) {
                    rva = val - 0x400000;
                }
                if (rva < 0 || rva > 0x2000000) continue;

                Address target = program.getImageBase().add(rva);
                if (!memory.contains(target)) continue;

                /* Check if in an executable block */
                MemoryBlock block = memory.getBlock(target);
                if (block == null || !block.isExecute()) continue;

                Function func = program.getFunctionManager().getFunctionAt(target);
                String funcName = (func != null) ? func.getName() : "(no func)";

                println("  [" + addr + "] -> " + target + " " + funcName);

                /* Print first 24 bytes as pattern */
                byte[] bytes = new byte[24];
                if (memory.getBytes(target, bytes) == 24) {
                    StringBuilder pat = new StringBuilder();
                    for (int j = 0; j < 24; j++) {
                        if (j > 0) pat.append(" ");
                        pat.append(String.format("%02X", bytes[j] & 0xFF));
                    }
                    println("      Pattern: " + pat);
                }
            } catch (Exception e) {
                continue;
            }
        }

        println("");
        println("=== Done ===");
        println("Look for a function whose disassembly calls into Eng.dll or Core.dll");
        println("with an FArchive pointer — that's ULinkerLoad::Serialize.");
        println("Copy its pattern into hook_serialize.c: #define PTRN_SERIALIZE \"...\"");
    }

    private Address findStringContaining(String sub) {
        DataIterator it = getCurrentProgram().getListing().getDefinedData(true);
        while (it.hasNext() && !monitor.isCancelled()) {
            Data data = it.next();
            if (!data.hasStringValue()) continue;
            String val = data.getDefaultValueRepresentation();
            if (val.startsWith("\"") && val.endsWith("\"")) {
                val = val.substring(1, val.length() - 1);
            }
            if (val.contains(sub)) {
                return data.getAddress();
            }
        }
        return null;
    }

    private Address findTypeDescriptor(Address strAddr, Memory memory) {
        long target = strAddr.getOffset();
        for (long off = -256; off <= 256; off += 4) {
            try {
                Address addr = strAddr.getNewAddress(strAddr.getOffset() + off);
                long val = memory.getInt(addr) & 0xFFFFFFFFL;
                if (val == target) {
                    return addr.subtract(8);
                }
            } catch (Exception e) {
                continue;
            }
        }
        return null;
    }

    private void printStringRefs(Address strAddr, ReferenceManager refMgr) {
        ReferenceIterator it = refMgr.getReferencesTo(strAddr);
        int count = 0;
        while (it.hasNext() && count < 20 && !monitor.isCancelled()) {
            println("  Ref from: " + it.next().getFromAddress());
            count++;
        }
    }
}
