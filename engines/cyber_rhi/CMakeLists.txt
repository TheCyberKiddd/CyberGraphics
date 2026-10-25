set(CYBER_RHI_SOURCES
    src/device.cpp
    src/null_backend.cpp
)

set(CYBER_RHI_HEADERS
    include/cyber/rhi/backend.h
    include/cyber/rhi/capabilities.h
    include/cyber/rhi/conventions.h
    include/cyber/rhi/descriptors.h
    include/cyber/rhi/device.h
    include/cyber/rhi/format.h
    include/cyber/rhi/handle.h
    include/cyber/rhi/rhi.h
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
