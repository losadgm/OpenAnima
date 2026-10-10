# -----------------------------------------------------------------------------
#  OpenNPC core library.
#  Static or shared depending on BUILD_SHARED_LIBS (defaults to static).
# -----------------------------------------------------------------------------
add_library(OpenNPC
    NPCBrain.cpp)

add_library(OpenNPC::OpenNPC ALIAS OpenNPC)

target_compile_features(OpenNPC PUBLIC cxx_std_17)

target_include_directories(OpenNPC
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>
        $<INSTALL_INTERFACE:include>)

target_link_libraries(OpenNPC
    PUBLIC
        nlohmann_json::nlohmann_json)

if(MSVC)
    target_compile_options(OpenNPC PRIVATE /W4 /permissive-)
else()
    target_compile_options(OpenNPC PRIVATE -Wall -Wextra -Wpedantic)
endif()

set_target_properties(OpenNPC PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR})
