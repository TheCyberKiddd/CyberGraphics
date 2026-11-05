cyber_add_module(
    NAME renderer
    LAYER 3
    SOURCES
        src/hot_pipeline.cpp
        src/reflected_layout.cpp
    PUBLIC_HEADERS
        include/cyber/renderer/hot_pipeline.h
        include/cyber/renderer/reflected_layout.h
    DEPENDS core memory jobs rhi shaders
)

cyber_add_test(
    NAME renderer_reflected_layout
    SOURCES
        tests/reflected_layout_test.cpp
    DEPENDS renderer shaders rhi memory
)

cyber_add_test(
    NAME renderer_hot_pipeline
    SOURCES
        tests/hot_pipeline_test.cpp
    DEPENDS renderer shaders rhi jobs memory
)

cyber_add_test(
    NAME renderer_public_headers
    SOURCES
        tests/public_headers_test.cpp
    DEPENDS renderer
)
