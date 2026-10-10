# -----------------------------------------------------------------------------
#  Example programs. `cli_harness` is a headless smoke test for the library.
# -----------------------------------------------------------------------------
add_executable(open_npc_cli
    cli_harness/main.cpp)

target_link_libraries(open_npc_cli
    PRIVATE
        OpenNPC::OpenNPC)
