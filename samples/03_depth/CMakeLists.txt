add_executable(cyber_sample_depth main.cpp)

target_link_libraries(cyber_sample_depth PRIVATE
    cyber::software_renderer
    cyber::platform
    cyber::memory
    cyber::jobs
)
target_compile_features(cyber_sample_depth PRIVATE cxx_std_20)
cyber_apply_warnings(cyber_sample_depth)

if(CYBER_RUNTIME_OPTIONS)
    target_compile_options(cyber_sample_depth PRIVATE ${CYBER_RUNTIME_OPTIONS})
endif()

set_target_properties(cyber_sample_depth PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)
