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

# -----------------------------------------------------------------------------
#  `tuiHarness` is the terminal client. It reads the environment the same way the
#  command line does, keeps one conversation and paints the answer as it arrives,
#  in the last lines of the terminal. The terminal layer has a Unix backend and a
#  Windows one, and the screen logic lives apart from both so it can be tested.
# -----------------------------------------------------------------------------
add_executable(open_anima_tui
    tuiHarness/main.cpp
    tuiHarness/app/commands.cpp
    tuiHarness/app/model.cpp
    tuiHarness/screen/render.cpp
    tuiHarness/screen/text.cpp
    tuiHarness/terminal/terminalUnix.cpp
    tuiHarness/terminal/terminalWindows.cpp
    cliHarness/dotenv.cpp)

target_include_directories(open_anima_tui
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/cliHarness
        ${CMAKE_CURRENT_SOURCE_DIR}/tuiHarness)

target_link_libraries(open_anima_tui
    PRIVATE
        OpenAnima::OpenAnima)

open_anima_enable_warnings(open_anima_tui)
