add_executable(injector
    main.c
)
target_compile_definitions(injector PRIVATE _WIN32_WINNT=0x0600)

# Copy hook.dll next to injector.exe so it can find it at runtime
add_custom_command(TARGET injector POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:hook>
        $<TARGET_FILE_DIR:injector>
)
