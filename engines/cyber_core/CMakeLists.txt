cyber_add_module(
    NAME core
    LAYER 0
    SOURCES
        src/version.cpp
    PUBLIC_HEADERS
        include/cyber/core/version.h
)

target_include_directories(cyber_core PUBLIC
    $<BUILD_INTERFACE:${CYBER_GENERATED_INCLUDE_DIR}>
)

cyber_add_test(
    NAME core_version
    SOURCES
        tests/version_test.cpp
    DEPENDS core
)
