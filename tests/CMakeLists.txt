add_executable(test_load test_load.c)
target_compile_definitions(test_load PRIVATE _WIN32_WINNT=0x0600)
add_dependencies(test_load hook)
add_custom_command(TARGET test_load POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:hook>
        $<TARGET_FILE_DIR:test_load>
)
add_test(NAME hook_loads
    COMMAND test_load
    WORKING_DIRECTORY $<TARGET_FILE_DIR:test_load>
)
set_tests_properties(hook_loads PROPERTIES TIMEOUT 15)

add_executable(test_pipe_server test_pipe_server.c)
target_compile_definitions(test_pipe_server PRIVATE _WIN32_WINNT=0x0600)
add_test(NAME pipe_sends
    COMMAND test_pipe_server
)

add_executable(test_pattern test_pattern.c
    ${CMAKE_SOURCE_DIR}/hook/pattern.c
)
target_include_directories(test_pattern PRIVATE ${CMAKE_SOURCE_DIR}/hook)
target_compile_definitions(test_pattern PRIVATE _WIN32_WINNT=0x0600)
add_test(NAME pattern_scan
    COMMAND test_pattern
)
