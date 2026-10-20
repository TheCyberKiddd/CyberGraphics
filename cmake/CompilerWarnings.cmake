include_guard(GLOBAL)

# Apply the project warning policy to a target. Warnings are a build policy,
# not a link dependency, so they never propagate to consumers or into the
# installed package.
function(cyber_apply_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE
            /W4
            /permissive-
            /Zc:__cplusplus
            /utf-8
        )
        if(CYBER_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wshadow
        )
        if(CYBER_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()
