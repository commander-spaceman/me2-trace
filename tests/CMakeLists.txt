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

add_executable(test_pipe_server test_pipe_server.c)
target_compile_definitions(test_pipe_server PRIVATE _WIN32_WINNT=0x0600)
add_test(NAME pipe_sends
    COMMAND test_pipe_server
)
