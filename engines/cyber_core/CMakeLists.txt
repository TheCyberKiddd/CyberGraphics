cyber_add_module(
    NAME core
    LAYER 0
    SOURCES
        src/assert.cpp
        src/hash.cpp
        src/log.cpp
        src/log_sinks.cpp
        src/version.cpp
    PUBLIC_HEADERS
        include/cyber/core/assert.h
        include/cyber/core/build_config.h
        include/cyber/core/error.h
        include/cyber/core/hash.h
        include/cyber/core/log.h
        include/cyber/core/log_sinks.h
        include/cyber/core/result.h
        include/cyber/core/types.h
        include/cyber/core/version.h
)

target_include_directories(cyber_core PUBLIC
    $<BUILD_INTERFACE:${CYBER_GENERATED_INCLUDE_DIR}>
)

cyber_add_test(
    NAME core_result
    SOURCES
        tests/result_test.cpp
    DEPENDS core
)

cyber_add_test(
    NAME core_assert
    SOURCES
        tests/assert_test.cpp
    DEPENDS core
)

cyber_add_test(
    NAME core_log
    SOURCES
        tests/log_test.cpp
    DEPENDS core
)

cyber_add_test(
    NAME core_hash
    SOURCES
        tests/hash_test.cpp
    DEPENDS core
)

cyber_add_test(
    NAME core_build_config
    SOURCES
        tests/build_config_test.cpp
    DEPENDS core
)

cyber_add_test(
    NAME core_version
    SOURCES
        tests/version_test.cpp
    DEPENDS core
)

cyber_add_test(
    NAME core_public_headers
    SOURCES
        tests/public_headers_test.cpp
    DEPENDS core
)
