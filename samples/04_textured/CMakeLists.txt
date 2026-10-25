add_executable(cyber_sample_textured main.cpp)

target_link_libraries(cyber_sample_textured PRIVATE
    cyber::software_renderer
    cyber::platform
    cyber::memory
    cyber::jobs
)
target_compile_features(cyber_sample_textured PRIVATE cxx_std_20)
cyber_apply_warnings(cyber_sample_textured)

if(CYBER_RUNTIME_OPTIONS)
    target_compile_options(cyber_sample_textured PRIVATE ${CYBER_RUNTIME_OPTIONS})
endif()

set_target_properties(cyber_sample_textured PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)
