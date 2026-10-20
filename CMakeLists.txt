cmake_minimum_required(VERSION 3.28)

project(CyberGraphics
    VERSION 0.1.0
    DESCRIPTION "Cyber Engine's own renderer"
    LANGUAGES CXX
)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

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

enable_testing()

# The stable product contract. Both renderer plugins implement this interface;
# it contains no implementation and depends on no module.
add_library(cyber_graphics_api INTERFACE)
add_library(cyber::graphics_api ALIAS cyber_graphics_api)
target_include_directories(cyber_graphics_api INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_compile_features(cyber_graphics_api INTERFACE cxx_std_20)

# Active modules. Add a subdirectory only when its phase starts. Planned
# modules stay documented in engines/MODULES.md until then.
add_subdirectory(engines/cyber_core)
