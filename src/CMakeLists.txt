# -----------------------------------------------------------------------------
#  OpenAnima core library.
#  Static or shared depending on BUILD_SHARED_LIBS (defaults to static).
# -----------------------------------------------------------------------------
add_library(OpenAnima
    Core/NPCBrain.cpp
    Provider/ProviderConfig.cpp
    Provider/RestProvider.cpp
    OpenAnima.cpp)

if(OPENANIMA_WITH_CURL)
    target_sources(OpenAnima
        PRIVATE
            Http/HttpTransportCurl.cpp)
    # The facade exposes makeNpc() overloads that build the default transport, so the
    # umbrella header needs to know whether that transport was compiled in.
    target_compile_definitions(OpenAnima PUBLIC OPENANIMA_HAS_CURL)
endif()

add_library(OpenAnima::OpenAnima ALIAS OpenAnima)

target_compile_features(OpenAnima PUBLIC cxx_std_17)

target_include_directories(OpenAnima
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>
        $<INSTALL_INTERFACE:include>)

target_link_libraries(OpenAnima
    PUBLIC
        nlohmann_json::nlohmann_json)

# The HTTP client is an implementation detail: consumers see HttpTransport only,
# so its headers and libraries never leak into their builds.
if(OPENANIMA_WITH_CURL)
    target_link_libraries(OpenAnima
        PRIVATE
            ${OPENANIMA_CURL_TARGET})
endif()

if(MSVC)
    target_compile_options(OpenAnima PRIVATE /W4 /permissive-)
else()
    target_compile_options(OpenAnima PRIVATE -Wall -Wextra -Wpedantic)
endif()

set_target_properties(OpenAnima PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR})
