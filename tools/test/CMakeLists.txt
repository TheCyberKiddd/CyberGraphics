# The project test runner. It is a developer tool: never installed, never
# linked into a shipping module, and allowed to use the standard library
# normally. Runtime modules keep exceptions and RTTI disabled.
add_library(cyber_test STATIC
    src/test.cpp
    src/runner_main.cpp
)
add_library(cyber::test ALIAS cyber_test)

target_include_directories(cyber_test PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_features(cyber_test PUBLIC cxx_std_20)
cyber_apply_warnings(cyber_test)

if(MSVC)
    target_compile_options(cyber_test PRIVATE /EHsc)
else()
    target_compile_options(cyber_test PRIVATE -fexceptions)
endif()

set_target_properties(cyber_test PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)

cyber_add_test(
    NAME test_runner
    SOURCES
        tests/test_runner_test.cpp
)
