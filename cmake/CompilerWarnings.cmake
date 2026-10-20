include_guard(GLOBAL)

add_library(cyber_warnings INTERFACE)
add_library(cyber::warnings ALIAS cyber_warnings)

if(MSVC)
    target_compile_options(cyber_warnings INTERFACE
        /W4
        /permissive-
        /Zc:__cplusplus
        /utf-8
    )
    if(CYBER_WARNINGS_AS_ERRORS)
        target_compile_options(cyber_warnings INTERFACE /WX)
    endif()
else()
    target_compile_options(cyber_warnings INTERFACE
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
    )
    if(CYBER_WARNINGS_AS_ERRORS)
        target_compile_options(cyber_warnings INTERFACE -Werror)
    endif()
endif()
