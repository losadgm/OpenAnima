# -----------------------------------------------------------------------------
#  Example programs. `cli_harness` is a headless smoke test for the library.
# -----------------------------------------------------------------------------
add_executable(open_anima_cli
    cli_harness/main.cpp)

target_link_libraries(open_anima_cli
    PRIVATE
        OpenAnima::OpenAnima)

# The harness talks to a real endpoint, so it needs the backend to be compiled in.
if(OPENANIMA_WITH_CURL)
    target_compile_definitions(open_anima_cli
        PRIVATE
            OPENANIMA_WITH_CURL=1)
endif()
