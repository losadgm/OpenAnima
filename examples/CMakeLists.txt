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
#  Prototypes of the terminal client. Throwaway code: two candidates for the same
#  job, to be compared and deleted, so they are kept out of the build unless asked
#  for with -DOPENANIMA_BUILD_PROTOTYPES=ON.
# -----------------------------------------------------------------------------
option(OPENANIMA_BUILD_PROTOTYPES "Build the terminal client prototypes" OFF)

if(OPENANIMA_BUILD_PROTOTYPES)
    # FTXUI, only for the prototype that tries it out: the point of the exercise is to
    # see what having it as a dependency of a client would cost.
    include(FetchContent)
    set(OPENANIMA_FTXUI_TAG "v7.0.3" CACHE STRING "FTXUI revision the prototype builds against")
    FetchContent_Declare(
        ftxui
        GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI.git
        GIT_TAG        ${OPENANIMA_FTXUI_TAG}
        GIT_SHALLOW    TRUE)

    set(FTXUI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(FTXUI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(FTXUI_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(FTXUI_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
    set(FTXUI_QUIET ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(ftxui)

    add_executable(tui_prototype_handwritten
        tuiPrototype/handwritten/main.cpp
        tuiPrototype/handwritten/terminal.cpp
        cliHarness/dotenv.cpp)

    add_executable(tui_prototype_ftxui
        tuiPrototype/ftxui/main.cpp
        cliHarness/dotenv.cpp)

    # The environment loader is the one the command line client already has.
    foreach(prototype IN ITEMS tui_prototype_handwritten tui_prototype_ftxui)
        target_include_directories(${prototype}
            PRIVATE
                ${CMAKE_CURRENT_SOURCE_DIR}/cliHarness)

        target_link_libraries(${prototype}
            PRIVATE
                OpenAnima::OpenAnima)

        open_anima_enable_warnings(${prototype})
    endforeach()

    target_link_libraries(tui_prototype_ftxui
        PRIVATE
            ftxui::component
            ftxui::dom
            ftxui::screen)
endif()
