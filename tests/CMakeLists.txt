# -----------------------------------------------------------------------------
#  Test suite (Catch2 v3). Registered with CTest via catch_discover_tests.
# -----------------------------------------------------------------------------
add_executable(open_anima_tests
    test_npc_brain.cpp
    test_provider_config.cpp
    test_rest_provider.cpp
    fake_http_transport.h)

target_link_libraries(open_anima_tests
    PRIVATE
        OpenAnima::OpenAnima
        Catch2::Catch2WithMain)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(open_anima_tests)
