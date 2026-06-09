add_executable(viewer
    main.c
)
target_compile_definitions(viewer PRIVATE _WIN32_WINNT=0x0600)
