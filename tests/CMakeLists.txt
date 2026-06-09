add_executable(test_load test_load.c)
target_compile_definitions(test_load PRIVATE _WIN32_WINNT=0x0600)

# Ensure hook.dll is built before tests run
add_dependencies(test_load hook)

# Copy hook.dll next to test executable
add_custom_command(TARGET test_load POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:hook>
        $<TARGET_FILE_DIR:test_load>
)

add_test(NAME hook_loads
    COMMAND test_load
    WORKING_DIRECTORY $<TARGET_FILE_DIR:test_load>
)

add_test(NAME hook_builds
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target hook
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)
