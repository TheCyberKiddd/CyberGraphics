set(CYBER_RHI_SOURCES
    src/device.cpp
    src/null_backend.cpp
    src/pipeline.cpp
)

set(CYBER_RHI_HEADERS
    include/cyber/rhi/backend.h
    include/cyber/rhi/binding.h
    include/cyber/rhi/buffer.h
    include/cyber/rhi/capabilities.h
    include/cyber/rhi/conventions.h
    include/cyber/rhi/descriptors.h
    include/cyber/rhi/device.h
    include/cyber/rhi/format.h
    include/cyber/rhi/handle.h
    include/cyber/rhi/pipeline.h
    include/cyber/rhi/resource_state.h
    include/cyber/rhi/rhi.h
    include/cyber/rhi/sampler.h
    include/cyber/rhi/shader.h
    include/cyber/rhi/texture.h
)

cyber_add_module(
    NAME rhi
    LAYER 2
    SOURCES ${CYBER_RHI_SOURCES}
    PUBLIC_HEADERS ${CYBER_RHI_HEADERS}
    DEPENDS core memory
)

cyber_add_test(
    NAME rhi_public_headers
    SOURCES tests/public_headers_test.cpp
    DEPENDS rhi memory
)

cyber_add_test(
    NAME rhi_handle
    SOURCES tests/handle_test.cpp
    DEPENDS rhi
)

cyber_add_test(
    NAME rhi_format
    SOURCES tests/format_test.cpp
    DEPENDS rhi
)

cyber_add_test(
    NAME rhi_conventions
    SOURCES tests/conventions_test.cpp
    DEPENDS rhi
)

cyber_add_test(
    NAME rhi_capabilities
    SOURCES tests/capabilities_test.cpp
    DEPENDS rhi memory
)

cyber_add_test(
    NAME rhi_device
    SOURCES tests/device_test.cpp
    DEPENDS rhi memory
)

cyber_add_test(
    NAME rhi_fence
    SOURCES tests/fence_test.cpp
    DEPENDS rhi memory
)

cyber_add_test(
    NAME rhi_format_table
    SOURCES tests/format_table_test.cpp
    DEPENDS rhi
)

cyber_add_test(
    NAME rhi_buffer
    SOURCES tests/buffer_test.cpp
    DEPENDS rhi memory
)

cyber_add_test(
    NAME rhi_texture
    SOURCES tests/texture_test.cpp
    DEPENDS rhi memory
)

cyber_add_test(
    NAME rhi_sampler
    SOURCES tests/sampler_test.cpp
    DEPENDS rhi memory
)

cyber_add_test(
    NAME rhi_resource_state
    SOURCES tests/resource_state_test.cpp
    DEPENDS rhi memory
)

cyber_add_test(
    NAME rhi_shader
    SOURCES tests/shader_test.cpp
    DEPENDS rhi memory
)

cyber_add_test(
    NAME rhi_binding
    SOURCES tests/binding_test.cpp
    DEPENDS rhi memory
)

cyber_add_test(
    NAME rhi_pipeline
    SOURCES tests/pipeline_test.cpp
    DEPENDS rhi memory
)

cyber_add_test(
    NAME rhi_pipeline_hash
    SOURCES tests/pipeline_hash_test.cpp
    DEPENDS rhi
)
