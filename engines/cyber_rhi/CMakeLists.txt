set(CYBER_RHI_SOURCES
    src/command_list.cpp
    # Compiled on every platform: without the SDK the file's fallback branch
    # defines `D3D12Backend()` to return null, so the symbol `d3d12.h` declares
    # always exists and a consumer never fails to link. This is the only source
    # in the renderer that includes a graphics API header, and it does so behind
    # `CYBER_RHI_HAS_D3D12`.
    src/d3d12/d3d12_backend.cpp
    src/d3d12/d3d12_device.cpp
    src/d3d12/d3d12_binding.cpp
    src/d3d12/d3d12_pipelines.cpp
    src/d3d12/d3d12_commands.cpp
    src/d3d12/d3d12_swapchain.cpp
    src/d3d12/d3d12_resources.cpp
    src/d3d12/d3d12_textures.cpp
    src/d3d12/gpu_markers.cpp
    src/d3d12/gpu_allocator.cpp
    src/device.cpp
    src/frame_ring.cpp
    src/null_backend.cpp
    src/validation.cpp
    src/validating_device.cpp
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
    include/cyber/rhi/d3d12.h
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
    include/cyber/rhi/memory.h
)

cyber_add_module(
    NAME rhi
    LAYER 2
    SOURCES ${CYBER_RHI_SOURCES}
    PUBLIC_HEADERS ${CYBER_RHI_HEADERS}
    DEPENDS core memory
)

target_compile_definitions(cyber_rhi PRIVATE
    "$<$<CONFIG:Debug>:CYBER_GPU_MARKERS=1>"
    "$<$<CONFIG:RelWithDebInfo>:CYBER_GPU_MARKERS=1>"
)

if(CYBER_RHI_ENABLE_D3D12)
    # Both are `PUBLIC` on purpose. The definition decides what
    # `IsD3D12CompiledIn()` returns, so it must agree with the binary a consumer
    # links. The libraries are `PUBLIC` because `cyber_rhi` is static: a consumer
    # that calls `D3D12Backend()` resolves its DXGI imports itself, and a
    # `PRIVATE` entry would leave those unresolved at link time.
    target_compile_definitions(cyber_rhi PUBLIC CYBER_RHI_HAS_D3D12=1)
    # `d3dcompiler` is the HLSL compiler that ships with the Windows SDK. Phase 16
    # replaces it with the project's own shader pipeline, and until then it is the
    # only way a shader becomes bytecode.
    target_link_libraries(cyber_rhi PUBLIC d3d12 dxgi d3dcompiler)
endif()

cyber_add_test(
    NAME rhi_validation_errors
    SOURCES tests/validation_errors_test.cpp
    DEPENDS rhi memory
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

if(CYBER_RHI_ENABLE_D3D12)
    # A standalone native probe separates adapter/runtime failures from RHI
    # initialization without loading or changing the renderer backend.
    add_executable(cyber_rhi_d3d12_probe EXCLUDE_FROM_ALL tests/d3d12_device_probe.cpp)
    target_compile_features(cyber_rhi_d3d12_probe PRIVATE cxx_std_20)
    target_link_libraries(cyber_rhi_d3d12_probe PRIVATE d3d12 dxgi)
    cyber_apply_warnings(cyber_rhi_d3d12_probe)

    # The adapter layer is the one part of the D3D12 backend that runs on any
    # Windows machine: WARP is always present, so CI exercises it without a GPU.
    cyber_add_test(
        NAME rhi_d3d12_adapters
        SOURCES tests/d3d12_adapters_test.cpp
        DEPENDS rhi memory
    )
    cyber_add_test(
        NAME rhi_d3d12_device
        SOURCES tests/d3d12_device_test.cpp
        DEPENDS rhi memory
    )
    cyber_add_test(
        NAME rhi_device_lost
        SOURCES tests/device_lost_test.cpp
        DEPENDS rhi memory
    )
    cyber_add_test(
        NAME rhi_d3d12_resources
        SOURCES tests/d3d12_resources_test.cpp
        DEPENDS rhi memory
    )
    cyber_add_test(
        NAME rhi_d3d12_binding
        SOURCES tests/d3d12_binding_test.cpp
        DEPENDS rhi memory
    )
    cyber_add_test(
        NAME rhi_d3d12_pipelines
        SOURCES tests/d3d12_pipelines_test.cpp
        DEPENDS rhi memory
    )
    cyber_add_test(
        NAME rhi_d3d12_commands
        SOURCES tests/d3d12_commands_test.cpp
        DEPENDS rhi memory
    )
    # Phase 15: the GPU heap allocator is a D3D12-internal component, but the
    # report it publishes is the public contract, so its test sits with the
    # backend tests rather than the null-backend ones.
    cyber_add_test(
        NAME rhi_gpu_memory
        SOURCES tests/gpu_memory_test.cpp
        DEPENDS rhi memory
    )
    cyber_add_test(
        NAME rhi_timestamps
        SOURCES tests/timestamps_test.cpp
        DEPENDS rhi memory
    )
endif()

# Names are read back from D3D12; marker ordering is tested on the validation
# backend and native marker support is checked against the active build mode.
cyber_add_test(
    NAME rhi_debug_markers
    SOURCES tests/debug_markers_test.cpp
    DEPENDS rhi memory
)

cyber_add_test(
    NAME rhi_leak_report
    SOURCES tests/leak_report_test.cpp
    DEPENDS rhi memory
)
