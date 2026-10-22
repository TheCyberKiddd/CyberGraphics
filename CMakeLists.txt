cmake_minimum_required(VERSION 3.28)

project(CyberGraphics
    VERSION 0.1.0
    DESCRIPTION "Cyber Engine's own renderer"
    LANGUAGES CXX
)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(CyberOptions)
include(CompilerOptions)
include(CompilerWarnings)
include(Platform)
include(Sanitizers)
include(CyberModule)
include(LayerCheck)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# The project version is defined once, here, and generated into a public
# header. No module hard-codes its own version string.
set(CYBER_GENERATED_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/include")
file(MAKE_DIRECTORY "${CYBER_GENERATED_INCLUDE_DIR}/cyber/core")
configure_file(
    cmake/version_generated.h.in
    "${CYBER_GENERATED_INCLUDE_DIR}/cyber/core/version_generated.h"
    @ONLY
)

if(CYBER_BUILD_TESTS)
    enable_testing()
    # The project test runner must exist before any module registers tests.
    add_subdirectory(tools/test)
endif()

# The stable product contract. Both renderer plugins implement this interface;
# it contains no implementation and depends on no module.
add_library(cyber_graphics_api INTERFACE)
add_library(cyber::graphics_api ALIAS cyber_graphics_api)
set_target_properties(cyber_graphics_api PROPERTIES EXPORT_NAME graphics_api)
target_include_directories(cyber_graphics_api INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_compile_features(cyber_graphics_api INTERFACE cxx_std_20)

# Active modules. Add a subdirectory only when its phase starts. Planned
# modules stay documented in engines/MODULES.md until then.
add_subdirectory(engines/cyber_core)
add_subdirectory(engines/cyber_math)
add_subdirectory(engines/cyber_memory)
add_subdirectory(engines/cyber_platform)
add_subdirectory(engines/cyber_perf)
add_subdirectory(engines/cyber_jobs)

if(CYBER_BUILD_SAMPLES)
    add_subdirectory(samples)
endif()

# Install and package rules are opt-out so a parent project that consumes the
# source tree can disable them with CYBER_INSTALL=OFF.
include(CyberPackage)
