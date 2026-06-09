cmake_minimum_required(VERSION 3.16)
project(me2-trace VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# MinHook static library
add_subdirectory(vendor)

# Hook DLL (injected into the game)
add_subdirectory(hook)

# Injector executable
add_subdirectory(injector)

# Viewer (external console for pipe output) — Phase 2+
# add_subdirectory(viewer)

# Tests
enable_testing()
add_subdirectory(tests)
