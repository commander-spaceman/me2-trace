add_executable(injector
    main.c
)

target_link_libraries(injector PRIVATE minhook)
target_compile_definitions(injector PRIVATE _WIN32_WINNT=0x0600)
