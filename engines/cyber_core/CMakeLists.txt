cyber_add_module(
    NAME core
    LAYER 0
    SOURCES
        src/version.cpp
    PUBLIC_HEADERS
        include/cyber/core/version.h
)

cyber_add_test(
    NAME core_version
    SOURCES
        tests/version_test.cpp
    DEPENDS core
)
