# -----------------------------------------------------------------------------
#  Example programs. `cliHarness` is the command-line client: it reads the
#  environment, keeps the conversation and prints the answer as it arrives.
# -----------------------------------------------------------------------------
add_executable(open_anima_cli
    cliHarness/main.cpp
    cliHarness/dotenv.cpp)

target_link_libraries(open_anima_cli
    PRIVATE
        OpenAnima::OpenAnima)

open_anima_enable_warnings(open_anima_cli)
