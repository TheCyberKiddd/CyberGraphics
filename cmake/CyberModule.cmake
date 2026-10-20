include_guard(GLOBAL)

# cyber_add_module(
#   NAME <short-name>
#   LAYER <0..4>
#   SOURCES <files...>
#   [PUBLIC_HEADERS <files...>]
#   [DEPENDS <module-or-contract...>]
#   [PRIVATE_DEPS <module-or-contract...>]
# )
function(cyber_add_module)
    cmake_parse_arguments(ARG "" "NAME;LAYER" "DEPENDS;SOURCES;PUBLIC_HEADERS;PRIVATE_DEPS" ${ARGN})

    if(NOT ARG_NAME)
        message(FATAL_ERROR "cyber_add_module: NAME is required")
    endif()
    if(NOT DEFINED ARG_LAYER)
        message(FATAL_ERROR "cyber_add_module(${ARG_NAME}): LAYER is required")
    endif()
    if(NOT ARG_SOURCES)
        message(FATAL_ERROR "cyber_add_module(${ARG_NAME}): SOURCES is required")
    endif()

    set(target "cyber_${ARG_NAME}")
    if(TARGET ${target})
        message(FATAL_ERROR "cyber_add_module: target ${target} already exists")
    endif()

    add_library(${target} STATIC ${ARG_SOURCES} ${ARG_PUBLIC_HEADERS})
    add_library(cyber::${ARG_NAME} ALIAS ${target})

    target_compile_features(${target} PUBLIC cxx_std_20)
    cyber_apply_warnings(${target})

    target_include_directories(${target}
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

    foreach(dep IN LISTS ARG_DEPENDS)
        cyber_link_dependency(${target} PUBLIC ${dep})
    endforeach()

    foreach(dep IN LISTS ARG_PRIVATE_DEPS)
        cyber_link_dependency(${target} PRIVATE ${dep})
    endforeach()

    if(CYBER_RUNTIME_OPTIONS)
        target_compile_options(${target} PRIVATE ${CYBER_RUNTIME_OPTIONS})
    endif()

    set_target_properties(${target} PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS NO
        POSITION_INDEPENDENT_CODE ON
        EXPORT_NAME ${ARG_NAME}
    )
endfunction()

function(cyber_link_dependency target visibility dep)
    if(dep STREQUAL "graphics_api")
        target_link_libraries(${target} ${visibility} cyber::graphics_api)
    elseif(dep MATCHES "^cyber_(.+)$")
        target_link_libraries(${target} ${visibility} cyber::${CMAKE_MATCH_1})
    else()
        target_link_libraries(${target} ${visibility} cyber::${dep})
    endif()
endfunction()

# cyber_add_test(
#   NAME <test-name>
#   SOURCES <files...>
#   [DEPENDS <module-or-contract...>]
# )
#
# The executable target is prefixed with cyber_test_ so it cannot collide with
# a module target. The CTest name remains the caller-provided NAME.
function(cyber_add_test)
    if(NOT CYBER_BUILD_TESTS)
        return()
    endif()

    cmake_parse_arguments(ARG "" "NAME" "SOURCES;DEPENDS" ${ARGN})

    if(NOT ARG_NAME)
        message(FATAL_ERROR "cyber_add_test: NAME is required")
    endif()
    if(NOT ARG_SOURCES)
        message(FATAL_ERROR "cyber_add_test(${ARG_NAME}): SOURCES is required")
    endif()

    get_property(known_tests GLOBAL PROPERTY CYBER_TEST_NAMES)
    if(ARG_NAME IN_LIST known_tests)
        message(FATAL_ERROR "cyber_add_test: duplicate test name ${ARG_NAME}")
    endif()
    set_property(GLOBAL APPEND PROPERTY CYBER_TEST_NAMES ${ARG_NAME})

    set(target "cyber_test_${ARG_NAME}")
    if(TARGET ${target})
        message(FATAL_ERROR "cyber_add_test: target ${target} already exists")
    endif()

    add_executable(${target} ${ARG_SOURCES})
    cyber_apply_warnings(${target})

    # Tests keep exceptions enabled so standard-library test helpers behave
    # normally; runtime modules use CYBER_RUNTIME_OPTIONS instead.
    if(MSVC)
        target_compile_options(${target} PRIVATE /EHsc)
    else()
        target_compile_options(${target} PRIVATE -fexceptions)
    endif()

    foreach(dep IN LISTS ARG_DEPENDS)
        cyber_link_dependency(${target} PRIVATE ${dep})
    endforeach()

    set_target_properties(${target} PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS NO
        OUTPUT_NAME ${ARG_NAME}
    )

    add_test(NAME ${ARG_NAME} COMMAND ${target})
endfunction()
