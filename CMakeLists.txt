# =============================================================================
#  OpenAnima - C++ Cognitive Middleware for AI Agents
#  Root build script (Modern CMake, target-based, C++17).
# =============================================================================
cmake_minimum_required(VERSION 3.20)

project(OpenAnima
    VERSION 0.1.0
    DESCRIPTION "C++ cognitive middleware for LLM-driven agents and NPCs"
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

FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

# Catch2 v3 is only required when building the test suite.
include(CTest)
if(BUILD_TESTING)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.5.2
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(Catch2)
endif()

# -----------------------------------------------------------------------------
#  Targets
# -----------------------------------------------------------------------------
add_subdirectory(src)

if(OPENANIMA_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
