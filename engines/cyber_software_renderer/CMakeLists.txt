set(CYBER_SOFTWARE_SOURCES
    src/debug_text.cpp
    src/frame_pacer.cpp
    src/framebuffer.cpp
    src/image_io.cpp
    src/present_target.cpp
)

set(CYBER_SOFTWARE_HEADERS
    include/cyber/software/debug_text.h
    include/cyber/software/frame_pacer.h
    include/cyber/software/framebuffer.h
    include/cyber/software/image_io.h
    include/cyber/software/present_target.h
    include/cyber/software/software.h
)

cyber_add_module(
    NAME software_renderer
    LAYER 4
    SOURCES ${CYBER_SOFTWARE_SOURCES}
    PUBLIC_HEADERS ${CYBER_SOFTWARE_HEADERS}
    DEPENDS core math memory platform jobs
)

cyber_add_test(
    NAME software_framebuffer
    SOURCES tests/framebuffer_test.cpp
    DEPENDS software_renderer memory
)

cyber_add_test(
    NAME software_present
    SOURCES tests/present_target_test.cpp
    DEPENDS software_renderer memory
)

cyber_add_test(
    NAME software_image_io
    SOURCES tests/image_io_test.cpp
    DEPENDS software_renderer memory platform
)

cyber_add_test(
    NAME software_text
    SOURCES tests/debug_text_test.cpp
    DEPENDS software_renderer memory
)

cyber_add_test(
    NAME software_frame_pacer
    SOURCES tests/frame_pacer_test.cpp
    DEPENDS software_renderer platform
)

cyber_add_test(
    NAME software_screenshot
    SOURCES tests/screenshot_test.cpp
    DEPENDS software_renderer memory platform
)

cyber_add_test(
    NAME software_public_headers
    SOURCES tests/public_headers_test.cpp
    DEPENDS software_renderer
)
