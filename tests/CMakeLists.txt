# -----------------------------------------------------------------------------
#  Test suite (Catch2 v3). Registered with CTest via catch_discover_tests.
# -----------------------------------------------------------------------------
add_executable(open_anima_tests
    Core/test_npc_brain.cpp
    Provider/test_rest_provider_config.cpp
    Provider/test_rest_provider.cpp
    Provider/test_sse_reader.cpp
    test_facade.cpp
    support/fake_http_transport.h
    support/fake_provider.h)

# The test sources include the shared fakes as "support/...", and the private
# headers of the library as "OpenAnima/Provider/...", which is where an
# implementation-only piece such as the SSE reader lives.
target_include_directories(open_anima_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../src)

target_link_libraries(open_anima_tests
    PRIVATE
        OpenAnima::OpenAnima
        Catch2::Catch2WithMain)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(open_anima_tests)
