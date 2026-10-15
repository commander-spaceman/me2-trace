# ME2 Trace

ME2 Trace is an experimental Windows C11 runtime-diagnostics tool for **Mass Effect 2 Original Trilogy**.

Its purpose is to provide limited context for a specific, reproducible package-writer validation failure—for example, whether a copied package was opened by the game or which selected package event occurred near the failure. It is not a PCC or TLK writer, parser, validator, or proof that the game successfully loaded a package.

## Current Design

The project is an x86 attach-mode tracing setup for the 32-bit `ME2Game.exe` process:

- `hook.dll` is loaded into an already-running game process.
- `injector.exe` loads the DLL and starts its post-loader initialization.
- `viewer.exe` receives events through the local `\\.\pipe\me2-trace` named pipe and writes a console log.
- The active hooks observe successful `CreateFileW` and `CloseHandle` calls for selected game-related extensions.
- The repository also contains basic DLL-load, named-pipe, and pattern-scanner tests.

The serialization-hook scaffold is present, but no `ULinkerLoad::Serialize` target has been verified and the baseline keeps that path disabled.

## Important Limits

Trace events describe Windows file-handle activity, not Unreal package parsing or deserialization success:

- `OPEN` means a selected file was opened through the observed API.
- `DONE` means a tracked handle was closed.
- `STUCK?` is a viewer warning about a long-lived tracked open.

None of these events proves that a PCC is valid or invalid. The current implementation also does not measure bytes read, and its file tracker is not independently synchronized across concurrent detour calls.

## Safety Boundary

Use ME2 Trace only for a documented diagnostic question and only with copied, controlled test assets. Never modify the game installation. Preserve case-specific hashes, commands, comparisons, logs, and observations outside the repository.

A successful build or basic test run does not prove injection safety, game compatibility, Unreal serialization behavior, or PCC writer correctness.
