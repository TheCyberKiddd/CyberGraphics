cyber_add_test(
    NAME phase23_geometry
    SOURCES phase23_geometry_test.cpp
    DEPENDS rhi scene software_renderer memory platform
)

if(TARGET cyber_test_phase23_geometry)
    target_include_directories(
        cyber_test_phase23_geometry
        PRIVATE "${CMAKE_SOURCE_DIR}/engines/cyber_rhi/tests"
    )
    target_compile_definitions(
        cyber_test_phase23_geometry
        PRIVATE CYBER_PHASE23_IMAGE_PATH="${CMAKE_BINARY_DIR}/phase23-lod-transition.ppm"
    )
endif()
