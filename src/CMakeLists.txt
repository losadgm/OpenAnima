# -----------------------------------------------------------------------------
#  OpenAnima core library.
#  Static or shared depending on BUILD_SHARED_LIBS (defaults to static).
# -----------------------------------------------------------------------------
add_library(OpenAnima
    NPCBrain.cpp)

add_library(OpenAnima::OpenAnima ALIAS OpenAnima)

target_compile_features(OpenAnima PUBLIC cxx_std_17)

target_include_directories(OpenAnima
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>
        $<INSTALL_INTERFACE:include>)

target_link_libraries(OpenAnima
    PUBLIC
        nlohmann_json::nlohmann_json)

if(MSVC)
    target_compile_options(OpenAnima PRIVATE /W4 /permissive-)
else()
    target_compile_options(OpenAnima PRIVATE -Wall -Wextra -Wpedantic)
endif()

set_target_properties(OpenAnima PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR})
