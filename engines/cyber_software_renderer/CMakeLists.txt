set(CYBER_SOFTWARE_SOURCES
    src/camera.cpp
    src/debug_text.cpp
    src/depth_buffer.cpp
    src/frame_pacer.cpp
    src/framebuffer.cpp
    src/golden_image.cpp
    src/image_io.cpp
    src/lighting.cpp
    src/mesh.cpp
    src/pipeline.cpp
    src/present_target.cpp
    src/raster.cpp
    src/shader.cpp
    src/texture.cpp
)

set(CYBER_SOFTWARE_HEADERS
    include/cyber/software/camera.h
    include/cyber/software/debug_text.h
    include/cyber/software/depth_buffer.h
    include/cyber/software/frame_pacer.h
    include/cyber/software/framebuffer.h
    include/cyber/software/golden_image.h
    include/cyber/software/image_io.h
    include/cyber/software/lighting.h
    include/cyber/software/mesh.h
    include/cyber/software/pipeline.h
    include/cyber/software/present_target.h
    include/cyber/software/raster.h
    include/cyber/software/shading.h
    include/cyber/software/shader.h
    include/cyber/software/texture.h
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

cyber_add_test(
    NAME software_raster
    SOURCES tests/raster_test.cpp
    DEPENDS software_renderer memory
)

cyber_add_test(
    NAME software_raster_coverage
    SOURCES tests/raster_coverage_test.cpp
    DEPENDS software_renderer memory jobs
)

cyber_add_test(
    NAME software_depth_buffer
    SOURCES tests/depth_buffer_test.cpp
    DEPENDS software_renderer memory
)

cyber_add_test(
    NAME software_camera
    SOURCES tests/camera_test.cpp
    DEPENDS software_renderer
)

cyber_add_test(
    NAME software_pipeline
    SOURCES tests/pipeline_test.cpp
    DEPENDS software_renderer memory jobs
)

cyber_add_test(
    NAME software_texture
    SOURCES tests/texture_test.cpp
    DEPENDS software_renderer memory
)

cyber_add_test(
    NAME software_shader
    SOURCES tests/shader_test.cpp
    DEPENDS software_renderer memory
)

cyber_add_test(
    NAME software_lighting
    SOURCES tests/lighting_test.cpp
    DEPENDS software_renderer memory
)

cyber_add_test(
    NAME software_image_load
    SOURCES tests/image_load_test.cpp
    DEPENDS software_renderer memory platform
)

cyber_add_test(
    NAME software_mesh
    SOURCES tests/mesh_test.cpp
    DEPENDS software_renderer memory platform
)

cyber_add_test(
    NAME software_golden_image
    SOURCES tests/golden_image_test.cpp
    DEPENDS software_renderer memory platform
)

cyber_add_test(
    NAME software_golden_scenes
    SOURCES tests/golden_scenes_test.cpp
    DEPENDS software_renderer memory platform
)

# Tests are skipped when a consumer adds this directory, so the target only
# exists in the CyberGraphics build.
if(TARGET cyber_test_software_golden_scenes)
    target_compile_definitions(
        cyber_test_software_golden_scenes
        PRIVATE CYBER_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/golden"
    )
endif()
