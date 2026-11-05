cyber_add_module(
    NAME shaders
    LAYER 2
    SOURCES
        src/compiler.cpp
    PUBLIC_HEADERS
        include/cyber/shaders/compiler.h
    DEPENDS core memory
)

cyber_add_test(
    NAME shaders_compiler
    SOURCES
        tests/compiler_test.cpp
    DEPENDS shaders memory
)
