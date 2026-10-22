set(CYBER_PLATFORM_SOURCES
    src/file_watcher.cpp
    src/filesystem.cpp
    src/input.cpp
    src/present.cpp
    src/thread.cpp
    src/time.cpp
    src/window.cpp
)

if(WIN32)
    list(APPEND CYBER_PLATFORM_SOURCES
        src/win32/executable_path_win32.cpp
        src/win32/library_win32.cpp
        src/win32/memory_mapped_file_win32.cpp
        src/win32/present_win32.cpp
        src/win32/thread_win32.cpp
        src/win32/time_win32.cpp
        src/win32/window_win32.cpp
    )
else()
    list(APPEND CYBER_PLATFORM_SOURCES
        src/null/present_null.cpp
        src/null/window_null.cpp
        src/posix/executable_path_posix.cpp
        src/posix/library_posix.cpp
        src/posix/memory_mapped_file_posix.cpp
        src/posix/thread_posix.cpp
        src/posix/time_posix.cpp
    )
endif()

set(CYBER_PLATFORM_HEADERS
    include/cyber/platform/filesystem.h
    include/cyber/platform/input.h
    include/cyber/platform/library.h
    include/cyber/platform/platform.h
    include/cyber/platform/present.h
    include/cyber/platform/thread.h
    include/cyber/platform/time.h
    include/cyber/platform/types.h
    include/cyber/platform/window.h
)

cyber_add_module(
    NAME platform
    LAYER 1
    SOURCES ${CYBER_PLATFORM_SOURCES}
    PUBLIC_HEADERS ${CYBER_PLATFORM_HEADERS}
    DEPENDS core memory
)

find_package(Threads REQUIRED)
target_link_libraries(cyber_platform PUBLIC Threads::Threads ${CMAKE_DL_LIBS})

if(WIN32)
    target_compile_definitions(cyber_platform PRIVATE _WIN32_WINNT=0x0A00)
endif()

cyber_add_test(
    NAME platform_time
    SOURCES tests/time_test.cpp
    DEPENDS platform
)

cyber_add_test(
    NAME platform_filesystem
    SOURCES tests/filesystem_test.cpp
    DEPENDS platform memory
)

cyber_add_test(
    NAME platform_window
    SOURCES tests/window_test.cpp
    DEPENDS platform memory
)

cyber_add_test(
    NAME platform_present
    SOURCES tests/present_test.cpp
    DEPENDS platform memory
)

cyber_add_test(
    NAME platform_input
    SOURCES tests/input_test.cpp
    DEPENDS platform memory
)

cyber_add_test(
    NAME platform_thread
    SOURCES tests/thread_test.cpp
    DEPENDS platform memory
)

cyber_add_test(
    NAME platform_library
    SOURCES tests/library_test.cpp
    DEPENDS platform
)

cyber_add_test(
    NAME platform_public_headers
    SOURCES tests/public_headers_test.cpp
    DEPENDS platform
)
