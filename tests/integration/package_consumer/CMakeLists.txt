cmake_minimum_required(VERSION 3.28)

project(CyberGraphicsPackageConsumer LANGUAGES CXX)

find_package(CyberGraphics 0.1 CONFIG REQUIRED)

add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE cyber::core cyber::math cyber::memory cyber::graphics_api)
target_compile_features(consumer PRIVATE cxx_std_20)
