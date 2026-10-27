# The Phase 14 image comparison.
#
# This is a cross-module test: it links the RHI (and through it the Direct3D 12
# backend) and the CPU reference renderer, which no module is allowed to do, so
# it lives under tests/ rather than inside a module.

cyber_add_test(
    NAME backend_compare
    SOURCES backend_compare_test.cpp
    DEPENDS rhi memory software_renderer platform
)

# The two images the phase's done-when line names are written here on every run,
# from the bytes the comparison itself read: a green test says the two
# rasterizers agree, and the images say what they rendered. They live in the
# build tree because they are regenerated on every run, not edited by hand, and
# the copies a report cites are the ones committed under `docs/reports/images/`.
if(TARGET cyber_test_backend_compare)
    target_compile_definitions(
        cyber_test_backend_compare
        PRIVATE CYBER_BACKEND_COMPARE_IMAGE_DIR="${CMAKE_BINARY_DIR}/backend_compare_images"
    )
endif()
