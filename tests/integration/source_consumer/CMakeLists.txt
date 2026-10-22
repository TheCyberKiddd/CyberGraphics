cmake_minimum_required(VERSION 3.28)

project(CyberGraphicsSourceConsumer LANGUAGES CXX)

set(CYBER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CYBER_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(CYBER_INSTALL OFF CACHE BOOL "" FORCE)
set(CYBER_WARNINGS_AS_ERRORS ON CACHE BOOL "" FORCE)

add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../../.." cyber_graphics)

add_executable(source_consumer ../package_consumer/main.cpp)
target_link_libraries(source_consumer PRIVATE
    cyber::core
    cyber::math
    cyber::memory
    cyber::platform
    cyber::perf
    cyber::jobs
    cyber::software_renderer
    cyber::graphics_api
)
target_compile_features(source_consumer PRIVATE cxx_std_20)
