cyber_add_module(
    NAME math
    LAYER 0
    SOURCES
        src/color.cpp
        src/frustum.cpp
        src/half.cpp
        src/mat3.cpp
        src/mat4.cpp
        src/quat.cpp
        src/ray.cpp
        src/transform.cpp
    PUBLIC_HEADERS
        include/cyber/math/aabb.h
        include/cyber/math/color.h
        include/cyber/math/constants.h
        include/cyber/math/frustum.h
        include/cyber/math/half.h
        include/cyber/math/mat3.h
        include/cyber/math/mat4.h
        include/cyber/math/math.h
        include/cyber/math/plane.h
        include/cyber/math/quat.h
        include/cyber/math/ray.h
        include/cyber/math/simd.h
        include/cyber/math/sphere.h
        include/cyber/math/transform.h
        include/cyber/math/types.h
        include/cyber/math/vec2.h
        include/cyber/math/vec3.h
        include/cyber/math/vec4.h
    DEPENDS core
)

cyber_add_test(
    NAME math_vec
    SOURCES
        tests/vec_test.cpp
    DEPENDS math
)

cyber_add_test(
    NAME math_mat
    SOURCES
        tests/mat_test.cpp
    DEPENDS math
)

cyber_add_test(
    NAME math_projection
    SOURCES
        tests/projection_test.cpp
    DEPENDS math
)

cyber_add_test(
    NAME math_quat
    SOURCES
        tests/quat_test.cpp
    DEPENDS math
)

cyber_add_test(
    NAME math_transform
    SOURCES
        tests/transform_test.cpp
    DEPENDS math
)

cyber_add_test(
    NAME math_geometry
    SOURCES
        tests/geometry_test.cpp
    DEPENDS math
)

cyber_add_test(
    NAME math_color
    SOURCES
        tests/color_test.cpp
    DEPENDS math
)

cyber_add_test(
    NAME math_half
    SOURCES
        tests/half_test.cpp
    DEPENDS math
)

cyber_add_test(
    NAME math_public_headers
    SOURCES
        tests/public_headers_test.cpp
    DEPENDS math
)

if(CYBER_BUILD_TESTS)
    add_executable(cyber_math_bench benchmarks/vec4_bench.cpp)
    target_link_libraries(cyber_math_bench PRIVATE cyber_math)
    target_compile_features(cyber_math_bench PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(cyber_math_bench PRIVATE /EHsc /GR)
    else()
        target_compile_options(cyber_math_bench PRIVATE -fexceptions -frtti)
    endif()
    cyber_apply_warnings(cyber_math_bench)
endif()
