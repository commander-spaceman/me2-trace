add_library(hook SHARED
    main.c
    pipe.c
)

set_target_properties(hook PROPERTIES
    PREFIX ""        # no "lib" prefix on Windows
    SUFFIX ".dll"
)

target_link_libraries(hook PRIVATE minhook)
target_compile_definitions(hook PRIVATE _WIN32_WINNT=0x0600)
