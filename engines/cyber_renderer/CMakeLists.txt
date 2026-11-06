cyber_add_module(
    NAME renderer
    LAYER 3
    SOURCES
        src/bindless_table.cpp
        src/descriptor_allocator.cpp
        src/frame_stream.cpp
        src/hot_pipeline.cpp
        src/material.cpp
        src/material_file.cpp
        src/reflected_layout.cpp
        src/render_graph.cpp
    PUBLIC_HEADERS
        include/cyber/renderer/bindless_table.h
        include/cyber/renderer/descriptor_allocator.h
        include/cyber/renderer/frame_stream.h
        include/cyber/renderer/hot_pipeline.h
        include/cyber/renderer/material.h
        include/cyber/renderer/reflected_layout.h
        include/cyber/renderer/render_graph.h
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

cyber_add_test(
    NAME renderer_material
    SOURCES
        tests/material_test.cpp
    DEPENDS renderer shaders rhi memory
)

cyber_add_test(
    NAME renderer_material_file
    SOURCES
        tests/material_file_test.cpp
    DEPENDS renderer
)

cyber_add_test(
    NAME renderer_phase19
    SOURCES
        tests/phase19_test.cpp
    DEPENDS renderer rhi memory
)

cyber_add_test(
    NAME renderer_render_graph
    SOURCES
        tests/render_graph_test.cpp
    DEPENDS renderer rhi memory
)
