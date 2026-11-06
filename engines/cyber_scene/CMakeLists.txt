cyber_add_module(
    NAME scene
    LAYER 2
    SOURCES
        src/mesh.cpp
        src/scene.cpp
    PUBLIC_HEADERS
        include/cyber/scene/mesh.h
        include/cyber/scene/scene.h
    DEPENDS core math memory jobs
)

cyber_add_test(
    NAME scene_geometry
    SOURCES
        tests/mesh_test.cpp
    DEPENDS scene
)

cyber_add_test(
    NAME scene_proxy
    SOURCES
        tests/scene_test.cpp
    DEPENDS scene jobs
)

cyber_add_test(
    NAME scene_render_thread
    SOURCES
        tests/render_thread_scene_test.cpp
    DEPENDS scene renderer
)

cyber_add_test(
    NAME scene_public_headers
    SOURCES
        tests/public_headers_test.cpp
    DEPENDS scene
)

if(CYBER_BUILD_TESTS)
    add_executable(cyber_scene_bench benchmarks/scene_culling_bench.cpp)
    target_link_libraries(cyber_scene_bench PRIVATE cyber_scene cyber_jobs cyber_memory)
    target_compile_features(cyber_scene_bench PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(cyber_scene_bench PRIVATE /EHsc /GR)
    else()
        target_compile_options(cyber_scene_bench PRIVATE -fexceptions -frtti)
    endif()
    cyber_apply_warnings(cyber_scene_bench)
endif()
