# -----------------------------------------------------------------------------
#  Test suite (Catch2 v3). Registered with CTest via catch_discover_tests.
# -----------------------------------------------------------------------------
add_executable(open_anima_tests
    Core/test_action_catalogue.cpp
    Core/test_npc_brain.cpp
    Core/test_npc_reply.cpp
    Provider/test_rest_provider_config.cpp
    Provider/test_rest_provider.cpp
    Provider/test_sse_reader.cpp
    cli/test_cli_options.cpp
    cli/test_cli_session.cpp
    tui/test_tui_canvas.cpp
    tui/test_tui_commands.cpp
    tui/test_tui_diff.cpp
    tui/test_tui_model.cpp
    tui/test_tui_render.cpp
    tui/test_tui_selection.cpp
    tui/test_tui_text.cpp
    ../examples/cliHarness/options.cpp
    ../examples/cliHarness/session.cpp
    ../examples/tuiHarness/app/commands.cpp
    ../examples/tuiHarness/app/model.cpp
    ../examples/tuiHarness/screen/canvas.cpp
    ../examples/tuiHarness/screen/cell.cpp
    ../examples/tuiHarness/screen/diff.cpp
    ../examples/tuiHarness/screen/render.cpp
    ../examples/tuiHarness/screen/selection.cpp
    ../examples/tuiHarness/screen/style.cpp
    ../examples/tuiHarness/screen/text.cpp
    test_facade.cpp
    support/fake_http_transport.h
    support/fake_provider.h)

# The test sources include the shared fakes as "support/...", the private
# headers of the library as "OpenAnima/Provider/...", which is where an
# implementation-only piece such as the SSE reader lives, and the pure parts of
# both clients as "app/...", "screen/..." and "cliHarness/...".
target_include_directories(open_anima_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../src
        ${CMAKE_CURRENT_SOURCE_DIR}/../examples
        ${CMAKE_CURRENT_SOURCE_DIR}/../examples/tuiHarness)

target_link_libraries(open_anima_tests
    PRIVATE
        OpenAnima::OpenAnima
        Catch2::Catch2WithMain)

# What --version prints, so the case that reads it sees the same value as the binary.
target_compile_definitions(open_anima_tests
    PRIVATE
        OPENANIMA_CLI_VERSION="${PROJECT_VERSION}")

open_anima_enable_warnings(open_anima_tests)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(open_anima_tests)
