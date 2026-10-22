include_guard(GLOBAL)

if(NOT CYBER_INSTALL)
    return()
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(CYBER_INSTALL_CMAKEDIR
    "${CMAKE_INSTALL_LIBDIR}/cmake/CyberGraphics"
    CACHE STRING "Install directory for CyberGraphics CMake package files"
)

install(
    TARGETS
        cyber_graphics_api
        cyber_core
        cyber_math
        cyber_memory
        cyber_platform
        cyber_perf
        cyber_jobs
    EXPORT CyberGraphicsTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(
    DIRECTORY engines/cyber_core/include/cyber/core
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/cyber
    FILES_MATCHING
        PATTERN "*.h"
        PATTERN "*.hpp"
)

install(
    DIRECTORY engines/cyber_math/include/cyber/math
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/cyber
    FILES_MATCHING
        PATTERN "*.h"
        PATTERN "*.hpp"
)

install(
    DIRECTORY engines/cyber_memory/include/cyber/memory
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/cyber
    FILES_MATCHING
        PATTERN "*.h"
        PATTERN "*.hpp"
)

install(
    DIRECTORY engines/cyber_platform/include/cyber/platform
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/cyber
    FILES_MATCHING
        PATTERN "*.h"
        PATTERN "*.hpp"
)

install(
    DIRECTORY engines/cyber_perf/include/cyber/perf
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/cyber
    FILES_MATCHING
        PATTERN "*.h"
        PATTERN "*.hpp"
)

install(
    DIRECTORY engines/cyber_jobs/include/cyber/jobs
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/cyber
    FILES_MATCHING
        PATTERN "*.h"
        PATTERN "*.hpp"
)

install(
    FILES
        ${CYBER_GENERATED_INCLUDE_DIR}/cyber/core/version_generated.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/cyber/core
)

install(
    DIRECTORY include/cybergraphics
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING
        PATTERN "*.h"
        PATTERN "*.hpp"
)

install(
    FILES LICENSE THIRD_PARTY_NOTICES.md
    DESTINATION ${CMAKE_INSTALL_DATADIR}/CyberGraphics
)

install(
    EXPORT CyberGraphicsTargets
    NAMESPACE cyber::
    DESTINATION ${CYBER_INSTALL_CMAKEDIR}
)

configure_package_config_file(
    cmake/CyberGraphicsConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/CyberGraphicsConfig.cmake"
    INSTALL_DESTINATION ${CYBER_INSTALL_CMAKEDIR}
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/CyberGraphicsConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(
    FILES
        "${CMAKE_CURRENT_BINARY_DIR}/CyberGraphicsConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/CyberGraphicsConfigVersion.cmake"
    DESTINATION ${CYBER_INSTALL_CMAKEDIR}
)
