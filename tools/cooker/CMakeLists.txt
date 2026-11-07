# Offline asset cooker. A build tool, not a runtime module: it may use the
# standard library freely but only touches module public APIs.
add_executable(cyber_cooker src/main.cpp)
target_link_libraries(cyber_cooker PRIVATE cyber::assets cyber::platform cyber::memory)
target_compile_features(cyber_cooker PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(cyber_cooker PRIVATE /EHsc /GR)
else()
    target_compile_options(cyber_cooker PRIVATE -fexceptions -frtti)
endif()
cyber_apply_warnings(cyber_cooker)

if(CYBER_BUILD_TESTS)
    set(CYBER_COOKER_FIXTURES "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/fixtures")
    set(CYBER_COOKER_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/fixtures_cooked")
    set(CYBER_COOKER_MANIFEST "${CYBER_COOKER_OUTPUT}/assets.json")
    set(CYBER_COOKER_STATS "${CYBER_COOKER_OUTPUT}/stats.json")
    set(CYBER_COOKER_REUSE_STATS "${CYBER_COOKER_OUTPUT}/stats_reuse.json")

    # Every run starts from an empty output directory, so a stale payload can
    # never make the checks below pass.
    add_test(NAME cooker_clean_output
             COMMAND ${CMAKE_COMMAND} -E rm -rf "${CYBER_COOKER_OUTPUT}")

    # The fixtures cover every readable source format, so one run proves the
    # whole pipeline instead of only the paths a single test file exercises.
    add_test(
        NAME cooker_cooks_fixtures
        COMMAND cyber_cooker
                --source "${CYBER_COOKER_FIXTURES}"
                --output "${CYBER_COOKER_OUTPUT}"
                --manifest "${CYBER_COOKER_MANIFEST}"
                --stats "${CYBER_COOKER_STATS}"
                --force
    )

    add_test(
        NAME cooker_verifies_output
        COMMAND ${CMAKE_COMMAND}
                "-DOUTPUT=${CYBER_COOKER_OUTPUT}"
                "-DCHECK_REUSE=OFF"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/verify_cooked.cmake"
    )
    set_tests_properties(cooker_cooks_fixtures PROPERTIES DEPENDS cooker_clean_output)
    set_tests_properties(cooker_verifies_output PROPERTIES DEPENDS cooker_cooks_fixtures)

    # A second run must trust the verified cache and cook nothing, which is what
    # keeps an incremental build cheap.
    add_test(
        NAME cooker_reuses_cache
        COMMAND cyber_cooker
                --source "${CYBER_COOKER_FIXTURES}"
                --output "${CYBER_COOKER_OUTPUT}"
                --manifest "${CYBER_COOKER_MANIFEST}"
                --stats "${CYBER_COOKER_REUSE_STATS}"
    )
    set_tests_properties(cooker_reuses_cache PROPERTIES DEPENDS cooker_verifies_output)

    add_test(
        NAME cooker_verifies_cache_reuse
        COMMAND ${CMAKE_COMMAND}
                "-DOUTPUT=${CYBER_COOKER_OUTPUT}"
                "-DCHECK_REUSE=ON"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/verify_cooked.cmake"
    )
    set_tests_properties(cooker_verifies_cache_reuse PROPERTIES DEPENDS cooker_reuses_cache)
endif()
