# =============================================================================
#  OpenAnima - middleware for conversational NPCs.
#  Root build script (Modern CMake, target-based, C++17).
# =============================================================================
cmake_minimum_required(VERSION 3.20)

project(OpenAnima
    VERSION 0.2.0
    DESCRIPTION "Middleware for conversational NPCs that interact with the player, the world and each other"
    HOMEPAGE_URL "https://github.com/losadgm/OpenAnima"
    LANGUAGES CXX)

# -----------------------------------------------------------------------------
#  Global language and build settings
# -----------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Emit compile_commands.json so clangd / clang-tidy see the real flags.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Give single-config generators (Ninja, Makefiles) a sane default build type.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# -----------------------------------------------------------------------------
#  Options
# -----------------------------------------------------------------------------
option(OPENANIMA_BUILD_EXAMPLES "Build the OpenAnima example programs" ON)
option(OPENANIMA_ENABLE_CLANG_TIDY "Run clang-tidy while compiling (clang only)" OFF)
option(OPENANIMA_ENABLE_DOCS "Generate the Doxygen API reference (docs target)" OFF)

# -----------------------------------------------------------------------------
#  Optional clang-tidy integration (works with the clang/LLVM toolchain only)
# -----------------------------------------------------------------------------
if(OPENANIMA_ENABLE_CLANG_TIDY)
    find_program(OPENANIMA_CLANG_TIDY_EXE NAMES clang-tidy clang-tidy-19 clang-tidy-18)
    if(NOT OPENANIMA_CLANG_TIDY_EXE)
        message(WARNING "OPENANIMA_ENABLE_CLANG_TIDY=ON but clang-tidy was not found.")
    else()
        set(CMAKE_CXX_CLANG_TIDY "${OPENANIMA_CLANG_TIDY_EXE}")
        message(STATUS "clang-tidy enabled: ${OPENANIMA_CLANG_TIDY_EXE}")
    endif()
endif()

# -----------------------------------------------------------------------------
#  Dependencies (fetched at configure time; nothing must be pre-installed)
# -----------------------------------------------------------------------------
include(FetchContent)

# CTest is included before the dependencies so that BUILD_TESTING already exists
# when curl is configured from source below.
include(CTest)

FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.12.0
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

# The async calls run on a thread, so the platform thread library is not optional,
# and a consumer that links this library has to get it too.
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# -----------------------------------------------------------------------------
#  HTTP transport backend.
#  The system libcurl is used when present; otherwise curl is built from source
#  with Schannel on Windows and OpenSSL elsewhere, so that nothing has to be
#  installed by hand on either platform. It is not optional: the public interface
#  is the same for every consumer, so the transport is always compiled in.
# -----------------------------------------------------------------------------
find_package(CURL QUIET)
if(CURL_FOUND)
    message(STATUS "HTTP transport: system libcurl ${CURL_VERSION_STRING}")
else()
    message(STATUS "HTTP transport: libcurl not found, building curl from source")
    # Only the library, and only the protocols a chat-completions call needs.
    set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_LIBCURL_DOCS OFF CACHE BOOL "" FORCE)
    set(BUILD_MISC_DOCS OFF CACHE BOOL "" FORCE)
    set(ENABLE_CURL_MANUAL OFF CACHE BOOL "" FORCE)
    set(HTTP_ONLY ON CACHE BOOL "" FORCE)
    set(CURL_DISABLE_LDAP ON CACHE BOOL "" FORCE)
    set(CURL_DISABLE_LDAPS ON CACHE BOOL "" FORCE)
    # Every optional dependency is switched off. curl treats libpsl as REQUIRED
    # and fails the whole configure when its header is missing, which is exactly
    # the situation on a clean runner. HTTPS plus JSON is all the provider needs.
    set(CURL_USE_LIBPSL OFF CACHE BOOL "" FORCE)
    set(CURL_USE_LIBSSH2 OFF CACHE BOOL "" FORCE)
    set(CURL_ZLIB OFF CACHE BOOL "" FORCE)
    set(CURL_BROTLI OFF CACHE BOOL "" FORCE)
    set(CURL_ZSTD OFF CACHE BOOL "" FORCE)
    set(USE_LIBIDN2 OFF CACHE BOOL "" FORCE)
    set(USE_NGHTTP2 OFF CACHE BOOL "" FORCE)
    if(WIN32)
        set(CURL_USE_SCHANNEL ON CACHE BOOL "" FORCE)
    else()
        set(CURL_USE_OPENSSL ON CACHE BOOL "" FORCE)
    endif()
    # curl registers its own test suite through BUILD_TESTING and flips
    # BUILD_SHARED_LIBS in the cache, both of which would leak into this project.
    # Build curl static and hand our own settings back afterwards.
    set(OPENANIMA_BUILD_TESTING_BACKUP ${BUILD_TESTING})
    set(OPENANIMA_BUILD_SHARED_LIBS_BACKUP "${BUILD_SHARED_LIBS}")
    set(BUILD_TESTING OFF)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
    FetchContent_Declare(
        curl
        GIT_REPOSITORY https://github.com/curl/curl.git
        GIT_TAG        curl-8_14_1
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(curl)
    set(BUILD_TESTING ${OPENANIMA_BUILD_TESTING_BACKUP})
    if(OPENANIMA_BUILD_SHARED_LIBS_BACKUP)
        set(BUILD_SHARED_LIBS "${OPENANIMA_BUILD_SHARED_LIBS_BACKUP}" CACHE BOOL
            "Build shared libraries" FORCE)
    else()
        unset(BUILD_SHARED_LIBS CACHE)
    endif()
endif()

if(TARGET CURL::libcurl)
    set(OPENANIMA_CURL_TARGET CURL::libcurl)
else()
    set(OPENANIMA_CURL_TARGET libcurl)
endif()

# Catch2 v3 is only required when building the test suite.
if(BUILD_TESTING)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.16.1
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(Catch2)
endif()

# -----------------------------------------------------------------------------
#  Warnings.
#  One project, one set of rules: the library, the tests and the examples are all
#  compiled with the same warnings, so nothing sneaks in through a target that
#  nobody looked at.
# -----------------------------------------------------------------------------
function(open_anima_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

# -----------------------------------------------------------------------------
#  Targets
# -----------------------------------------------------------------------------
# The words the terminal client says are configuration too: they live in data/tui-texts.json
# and are embedded as they are, so the client never opens a file at run time. The json is a
# configure dependency, so editing it and building again is enough.
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/data/tui-texts.json OPENANIMA_TUI_TEXTS_JSON)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
             ${CMAKE_CURRENT_SOURCE_DIR}/data/tui-texts.json)
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/examples/tuiHarness/domain/TuiTextsData.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/examples/generated/domain/TuiTextsData.h
    @ONLY)

add_subdirectory(src)

if(OPENANIMA_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

# -----------------------------------------------------------------------------
#  Documentation (optional).
#  docs/Doxyfile.in is a template; configure_file() substitutes the project
#  metadata. Configure with -DOPENANIMA_ENABLE_DOCS=ON to get a `docs` target.
# -----------------------------------------------------------------------------
if(OPENANIMA_ENABLE_DOCS)
    find_package(Doxygen QUIET)
    if(NOT DOXYGEN_FOUND)
        message(WARNING "OPENANIMA_ENABLE_DOCS=ON but Doxygen was not found.")
    else()
        configure_file(
            ${CMAKE_CURRENT_SOURCE_DIR}/docs/Doxyfile.in
            ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile
            @ONLY)
        add_custom_target(docs
            COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile
            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
            COMMENT "Generating the API reference with Doxygen"
            VERBATIM)
        message(STATUS "Documentation: target 'docs' with ${DOXYGEN_EXECUTABLE}")
    endif()
endif()
