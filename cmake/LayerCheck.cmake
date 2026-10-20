include_guard(GLOBAL)

find_package(Python3 COMPONENTS Interpreter QUIET)

if(Python3_Interpreter_FOUND)
    add_custom_target(cyber_layer_check
        COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/scripts/layer_check.py
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        COMMENT "Checking module dependency law"
        VERBATIM
    )

    add_custom_target(cyber_structure_check
        COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/scripts/check_structure.py
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        COMMENT "Checking repository structure and module contracts"
        VERBATIM
    )
else()
    message(STATUS "Python 3 not found; structure and layer check targets are unavailable")
endif()
