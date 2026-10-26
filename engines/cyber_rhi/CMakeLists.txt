set(CYBER_RHI_SOURCES
    src/command_list.cpp
    src/device.cpp
    src/frame_ring.cpp
    src/null_backend.cpp
    src/pipeline.cpp
    src/render_pass.cpp
    src/retirement.cpp
    src/submission.cpp
    src/swapchain.cpp
)

set(CYBER_RHI_HEADERS
    include/cyber/rhi/backend.h
    include/cyber/rhi/binding.h
    include/cyber/rhi/buffer.h
    include/cyber/rhi/capabilities.h
    include/cyber/rhi/command_list.h
    include/cyber/rhi/conventions.h
    include/cyber/rhi/descriptors.h
    include/cyber/rhi/device.h
    include/cyber/rhi/format.h
    include/cyber/rhi/frame_ring.h
    include/cyber/rhi/handle.h
    include/cyber/rhi/pipeline.h
    include/cyber/rhi/render_pass.h
    include/cyber/rhi/resource_state.h
    include/cyber/rhi/retirement.h
    include/cyber/rhi/rhi.h
    include/cyber/rhi/sampler.h
    include/cyber/rhi/shader.h
    include/cyber/rhi/submission.h
    include/cyber/rhi/swapchain.h
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

cyber_add_test(
    NAME rhi_render_pass
    SOURCES tests/render_pass_test.cpp
    DEPENDS rhi memory
)

cyber_add_test(
    NAME rhi_command_list
    SOURCES tests/command_list_test.cpp
    DEPENDS rhi memory
)

cyber_add_test(
    NAME rhi_submit
    SOURCES tests/submit_test.cpp
    DEPENDS rhi memory
)

cyber_add_test(
    NAME rhi_missing_barrier
    SOURCES tests/missing_barrier_test.cpp
    DEPENDS rhi memory
)

cyber_add_test(
    NAME rhi_frame_ring
    SOURCES tests/frame_ring_test.cpp
    DEPENDS rhi memory
)

cyber_add_test(
    NAME rhi_retirement
    SOURCES tests/retirement_test.cpp
    DEPENDS rhi memory
)

cyber_add_test(
    NAME rhi_swapchain
    SOURCES tests/swapchain_test.cpp
    DEPENDS rhi memory
)
