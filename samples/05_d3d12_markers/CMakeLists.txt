add_executable(cyber_sample_d3d12_markers main.cpp)

target_link_libraries(cyber_sample_d3d12_markers PRIVATE
    cyber::rhi
    cyber::platform
    cyber::memory
)
target_compile_features(cyber_sample_d3d12_markers PRIVATE cxx_std_20)
cyber_apply_warnings(cyber_sample_d3d12_markers)

if(CYBER_RUNTIME_OPTIONS)
    target_compile_options(cyber_sample_d3d12_markers PRIVATE ${CYBER_RUNTIME_OPTIONS})
endif()

set_target_properties(cyber_sample_d3d12_markers PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)
