cyber_add_module(
    NAME shaders
    LAYER 2
    SOURCES
        src/cache.cpp
        src/compiler.cpp
        src/library.cpp
        src/permutation.cpp
    PUBLIC_HEADERS
        include/cyber/shaders/cache.h
        include/cyber/shaders/compiler.h
        include/cyber/shaders/library.h
        include/cyber/shaders/permutation.h
        include/cyber/shaders/reflection.h
    DEPENDS core memory platform jobs
)

cyber_add_test(
    NAME shaders_compiler
    SOURCES
        tests/compiler_test.cpp
    DEPENDS shaders memory
)

cyber_add_test(
    NAME shaders_reflection
    SOURCES
        tests/reflection_test.cpp
    DEPENDS shaders memory
)

cyber_add_test(
    NAME shaders_permutation
    SOURCES
        tests/permutation_test.cpp
    DEPENDS shaders memory
)

cyber_add_test(
    NAME shaders_cache
    SOURCES
        tests/cache_test.cpp
    DEPENDS shaders memory platform
)

cyber_add_test(
    NAME shaders_library
    SOURCES
        tests/library_test.cpp
    DEPENDS shaders memory platform jobs
)

cyber_add_test(
    NAME shaders_public_headers
    SOURCES
        tests/public_headers_test.cpp
    DEPENDS shaders
)
