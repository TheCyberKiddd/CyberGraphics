add_executable(cyber_sample_frame_streaming main.cpp)

target_link_libraries(cyber_sample_frame_streaming PRIVATE
    cyber::core
    cyber::memory
    cyber::renderer
    cyber::rhi
)
target_compile_features(cyber_sample_frame_streaming PRIVATE cxx_std_20)
cyber_apply_warnings(cyber_sample_frame_streaming)

if(CYBER_RUNTIME_OPTIONS)
    target_compile_options(cyber_sample_frame_streaming PRIVATE ${CYBER_RUNTIME_OPTIONS})
endif()

set_target_properties(cyber_sample_frame_streaming PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)
