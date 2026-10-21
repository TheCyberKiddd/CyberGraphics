cyber_add_module(
    NAME memory
    LAYER 0
    SOURCES
        src/free_list_allocator.cpp
        src/heap_allocator.cpp
        src/linear_allocator.cpp
        src/pool_allocator.cpp
        src/ring_allocator.cpp
        src/string.cpp
        src/tracking_allocator.cpp
    PUBLIC_HEADERS
        include/cyber/memory/alignment.h
        include/cyber/memory/allocator.h
        include/cyber/memory/dynamic_array.h
        include/cyber/memory/fixed_array.h
        include/cyber/memory/frame_allocator.h
        include/cyber/memory/free_list_allocator.h
        include/cyber/memory/handle.h
        include/cyber/memory/hash_map.h
        include/cyber/memory/heap_allocator.h
        include/cyber/memory/linear_allocator.h
        include/cyber/memory/memory.h
        include/cyber/memory/pool_allocator.h
        include/cyber/memory/ring_allocator.h
        include/cyber/memory/slot_map.h
        include/cyber/memory/span.h
        include/cyber/memory/stack_allocator.h
        include/cyber/memory/string.h
        include/cyber/memory/string_view.h
        include/cyber/memory/tracking_allocator.h
        include/cyber/memory/types.h
    DEPENDS core
)

cyber_add_test(
    NAME memory_allocator
    SOURCES
        tests/allocator_test.cpp
    DEPENDS memory
)

cyber_add_test(
    NAME memory_container
    SOURCES
        tests/container_test.cpp
    DEPENDS memory
)

cyber_add_test(
    NAME memory_public_headers
    SOURCES
        tests/public_headers_test.cpp
    DEPENDS memory
)
