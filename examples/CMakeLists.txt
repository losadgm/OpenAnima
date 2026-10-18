# -----------------------------------------------------------------------------
#  Example programs. `cliHarness` is the command-line client: it reads the
#  environment, keeps the conversation and prints the answer as it arrives.
#  `common/` holds what both clients use: the API settings read from their
#  prefixed variables, and the loader for `.env`.
# -----------------------------------------------------------------------------
add_executable(open_anima_cli
    cliHarness/main.cpp
    cliHarness/options.cpp
    cliHarness/session.cpp
    common/apiSettings.cpp
    common/dotenv.cpp)

# The version printed by --version is the one the project declares, so it cannot drift.
target_compile_definitions(open_anima_cli
    PRIVATE
        OPENANIMA_CLI_VERSION="${PROJECT_VERSION}")

target_include_directories(open_anima_cli
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR})

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
    tuiHarness/application/brains.cpp
    tuiHarness/application/interaction.cpp
    tuiHarness/application/session.cpp
    tuiHarness/domain/commands.cpp
    tuiHarness/domain/model.cpp
    tuiHarness/domain/texts.cpp
    tuiHarness/presentation/canvas.cpp
    tuiHarness/presentation/cell.cpp
    tuiHarness/presentation/diff.cpp
    tuiHarness/presentation/render.cpp
    tuiHarness/presentation/selection.cpp
    tuiHarness/presentation/style.cpp
    tuiHarness/presentation/text.cpp
    tuiHarness/terminal/terminalUnix.cpp
    tuiHarness/terminal/terminalWindows.cpp
    common/apiSettings.cpp
    common/dotenv.cpp)

target_include_directories(open_anima_tui
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/tuiHarness
        ${CMAKE_BINARY_DIR}/examples/generated)

target_link_libraries(open_anima_tui
    PRIVATE
        OpenAnima::OpenAnima)

open_anima_enable_warnings(open_anima_tui)
