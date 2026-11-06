add_executable(cyber_sample_materials main.cpp)

target_link_libraries(cyber_sample_materials PRIVATE
    cyber::core
    cyber::memory
    cyber::platform
    cyber::renderer
    cyber::rhi
    cyber::shaders
)
target_compile_features(cyber_sample_materials PRIVATE cxx_std_20)
cyber_apply_warnings(cyber_sample_materials)

if(CYBER_RUNTIME_OPTIONS)
    target_compile_options(cyber_sample_materials PRIVATE ${CYBER_RUNTIME_OPTIONS})
endif()

set_target_properties(cyber_sample_materials PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)
