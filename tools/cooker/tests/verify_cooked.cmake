# Checks the output of a cyber_cooker run. Run with -DOUTPUT=<dir> and, for the
# second run of the cache test, -DCHECK_REUSE=ON.

if(NOT DEFINED OUTPUT)
    message(FATAL_ERROR "OUTPUT is required")
endif()

set(required_files
    assets.json
    quad.obj.cymesh
    png_rgba.png.cytex
    jpeg_420.jpg.cytex
    texture_bc1.dds.cytex
    texture_bc1.ktx2.cytex
    two_quads.gltf.cyscene
    two_quads.gltf_mesh0.cymesh
)
foreach(entry IN LISTS required_files)
    if(NOT EXISTS "${OUTPUT}/${entry}")
        message(FATAL_ERROR "cooker did not produce ${entry}")
    endif()
endforeach()

file(READ "${OUTPUT}/assets.json" manifest)
if(NOT manifest MATCHES "\"format\": \"cyberassetdb\"")
    message(FATAL_ERROR "manifest does not carry the database format tag")
endif()
if(NOT manifest MATCHES "png_rgba\\.png")
    message(FATAL_ERROR "manifest lost the scene image reference")
endif()
if(NOT manifest MATCHES "\"format\": \"derived\"")
    message(FATAL_ERROR "manifest has no derived asset records")
endif()
if(NOT manifest MATCHES "\"cookedPath\": \"two_quads\\.gltf_mesh0\\.cymesh\"")
    message(FATAL_ERROR "manifest does not point at the derived mesh payload")
endif()

if(CHECK_REUSE)
    if(NOT EXISTS "${OUTPUT}/stats_reuse.json")
        message(FATAL_ERROR "second run did not write stats")
    endif()
    file(READ "${OUTPUT}/stats_reuse.json" reuse_stats)

    foreach(counter cooked reused failed)
        if(NOT reuse_stats MATCHES "\"${counter}\": ([0-9]+)")
            message(FATAL_ERROR "stats file has no ${counter} counter")
        endif()
        set(${counter} "${CMAKE_MATCH_1}")
    endforeach()

    if(NOT cooked EQUAL 0)
        message(FATAL_ERROR "second run re-cooked ${cooked} asset(s) instead of reusing them")
    endif()
    if(NOT failed EQUAL 0)
        message(FATAL_ERROR "second run reported ${failed} failure(s)")
    endif()
    if(reused EQUAL 0)
        message(FATAL_ERROR "second run reused nothing, so the cache was not exercised")
    endif()
endif()
