# -----------------------------------------------------------------------------
#  Example programs. `cli_harness` is a headless smoke test for the library.
# -----------------------------------------------------------------------------
add_executable(open_anima_cli
    cli_harness/main.cpp
    cli_harness/dotenv.cpp)

target_link_libraries(open_anima_cli
    PRIVATE
        OpenAnima::OpenAnima)
