include_guard(GLOBAL)

# Project options are declared before any target is created so a parent project
# can override them when CyberGraphics is consumed as a subdirectory.
option(CYBER_SHIPPING "Build a shipping configuration" OFF)
option(CYBER_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)
option(CYBER_ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(CYBER_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
option(CYBER_ENABLE_TSAN "Enable ThreadSanitizer" OFF)
option(CYBER_NO_EXCEPTIONS "Disable C++ exceptions in runtime modules" ON)
option(CYBER_NO_RTTI "Disable RTTI in runtime modules" ON)
option(CYBER_MSVC_STATIC_RUNTIME "Link the MSVC runtime statically" OFF)

# The Direct3D 12 backend is Windows-only and needs the SDK that ships with the
# Visual Studio C++ workload. On any other platform it compiles out and
# `IsD3D12CompiledIn()` reports false.
if(WIN32)
    option(CYBER_RHI_ENABLE_D3D12 "Build the Direct3D 12 backend" ON)
else()
    set(CYBER_RHI_ENABLE_D3D12 OFF)
endif()

option(CYBER_BUILD_TESTS "Build the test suite" ON)
option(CYBER_BUILD_SAMPLES "Build samples" ON)
option(CYBER_BUILD_PLUGINS "Build renderer plugin products" OFF)
option(CYBER_INSTALL "Generate install and CMake package rules" ON)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(CYBER_IS_TOP_LEVEL ON)
else()
    set(CYBER_IS_TOP_LEVEL OFF)
endif()
