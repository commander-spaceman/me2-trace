/* DumpFunctions.java — Lists all functions containing "Serialize" or "Linker" in their name. */
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;

public class DumpFunctions extends GhidraScript {
    @Override
    public void run() throws Exception {
        FunctionIterator it = getCurrentProgram().getFunctionManager().getFunctions(true);
        while (it.hasNext() && !monitor.isCancelled()) {
            Function f = it.next();
            String name = f.getName();
            if (name.contains("Serialize") || name.contains("Linker") || name.contains("linker")) {
                byte[] bytes = new byte[24];
                getCurrentProgram().getMemory().getBytes(f.getEntryPoint(), bytes);
                StringBuilder pat = new StringBuilder();
                for (int i = 0; i < 24; i++) {
                    if (i > 0) pat.append(" ");
                    pat.append(String.format("%02X", bytes[i] & 0xFF));
                }
                println(f.getEntryPoint() + " " + name);
                println("  Pattern: " + pat);
            }
        }
        println("=== Done ===");
    }
}
