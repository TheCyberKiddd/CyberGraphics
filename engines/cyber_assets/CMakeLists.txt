cyber_add_module(
    NAME assets
    LAYER 2
    SOURCES
        src/asset.cpp
        src/asset_database.cpp
        src/asset_library.cpp
        src/cooked.cpp
        src/deflate.cpp
        src/dds.cpp
        src/jpeg.cpp
        src/json.cpp
        src/ktx2.cpp
        src/gltf.cpp
        src/material.cpp
        src/mesh_data.cpp
        src/obj.cpp
        src/png.cpp
        src/texture.cpp
    PUBLIC_HEADERS
        include/cyber/assets/asset.h
        include/cyber/assets/asset_database.h
        include/cyber/assets/asset_library.h
        include/cyber/assets/cooked.h
        include/cyber/assets/gltf.h
        include/cyber/assets/image.h
        include/cyber/assets/json.h
        include/cyber/assets/material.h
        include/cyber/assets/mesh_data.h
        include/cyber/assets/obj.h
        include/cyber/assets/texture.h
    DEPENDS core math memory platform
)

if(CYBER_BUILD_TESTS)
    cyber_add_test(
        NAME assets_json
        SOURCES
            tests/json_test.cpp
        DEPENDS assets
    )

    cyber_add_test(
        NAME assets_public_headers
        SOURCES
            tests/public_headers_test.cpp
        DEPENDS assets
    )

    # Fixtures live with the repository, not the build tree, so decoders are tested
    # against real committed bytes instead of generated data.
    file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/fixtures" CYBER_ASSET_FIXTURES)

    cyber_add_test(
        NAME assets_png
        SOURCES
            tests/png_test.cpp
        DEPENDS assets
    )
    target_compile_definitions(cyber_test_assets_png PRIVATE
        CYBER_ASSET_FIXTURES="${CYBER_ASSET_FIXTURES}")

    cyber_add_test(
        NAME assets_jpeg
        SOURCES
            tests/jpeg_test.cpp
        DEPENDS assets
    )
    target_compile_definitions(cyber_test_assets_jpeg PRIVATE
        CYBER_ASSET_FIXTURES="${CYBER_ASSET_FIXTURES}")

    cyber_add_test(
        NAME assets_texture
        SOURCES
            tests/texture_test.cpp
        DEPENDS assets
    )
    target_compile_definitions(cyber_test_assets_texture PRIVATE
        CYBER_ASSET_FIXTURES="${CYBER_ASSET_FIXTURES}")

    cyber_add_test(
        NAME assets_cooked
        SOURCES
            tests/cooked_test.cpp
        DEPENDS assets
    )
    target_compile_definitions(cyber_test_assets_cooked PRIVATE
        CYBER_ASSET_FIXTURES="${CYBER_ASSET_FIXTURES}")
    set_tests_properties(assets_cooked PROPERTIES TIMEOUT 20)

    cyber_add_test(
        NAME assets_obj
        SOURCES
            tests/obj_test.cpp
        DEPENDS assets
    )

    cyber_add_test(
        NAME assets_gltf
        SOURCES
            tests/gltf_test.cpp
        DEPENDS assets
    )
    target_compile_definitions(cyber_test_assets_gltf PRIVATE
        CYBER_ASSET_FIXTURES="${CYBER_ASSET_FIXTURES}")

    cyber_add_test(
        NAME assets_database
        SOURCES
            tests/asset_database_test.cpp
        DEPENDS assets
    )

    cyber_add_test(
        NAME assets_library
        SOURCES
            tests/asset_library_test.cpp
        DEPENDS assets
    )
    target_compile_definitions(cyber_test_assets_library PRIVATE
        CYBER_ASSET_FIXTURES="${CYBER_ASSET_FIXTURES}")

    cyber_add_test(
        NAME assets_fuzz
        SOURCES
            tests/fuzz_test.cpp
        DEPENDS assets
    )
    target_compile_definitions(cyber_test_assets_fuzz PRIVATE
        CYBER_ASSET_FIXTURES="${CYBER_ASSET_FIXTURES}")
    set_tests_properties(assets_fuzz PROPERTIES TIMEOUT 20)
endif()

# Not registered with CTest: the phase criterion needs measured evidence, and a
# shared CI machine cannot promise a stable ratio. Run it in the release profile.
if(CYBER_BUILD_TESTS)
    add_executable(cyber_assets_bench benchmarks/assets_load_bench.cpp)
    target_link_libraries(cyber_assets_bench PRIVATE cyber_assets cyber::platform cyber::memory)
    target_compile_features(cyber_assets_bench PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(cyber_assets_bench PRIVATE /EHsc /GR)
    else()
        target_compile_options(cyber_assets_bench PRIVATE -fexceptions -frtti)
    endif()
    cyber_apply_warnings(cyber_assets_bench)
    target_compile_definitions(cyber_assets_bench PRIVATE
        CYBER_ASSET_FIXTURES="${CYBER_ASSET_FIXTURES}")
endif()
