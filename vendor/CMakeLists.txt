add_library(minhook STATIC
    minhook/src/buffer.c
    minhook/src/hook.c
    minhook/src/trampoline.c
    minhook/src/hde/hde32.c
    minhook/src/hde/hde64.c
)

target_include_directories(minhook PUBLIC minhook/include)
target_compile_definitions(minhook PRIVATE _WIN32_WINNT=0x0600)
