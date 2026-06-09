add_executable(injector
    main.c
)
target_compile_definitions(injector PRIVATE _WIN32_WINNT=0x0600)
