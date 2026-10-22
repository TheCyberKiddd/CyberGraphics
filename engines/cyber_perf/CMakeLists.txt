set(CYBER_PERF_SOURCES
    src/profiler.cpp
)

set(CYBER_PERF_HEADERS
    include/cyber/perf/perf.h
    include/cyber/perf/profiler.h
)

cyber_add_module(
    NAME perf
    LAYER 1
    SOURCES ${CYBER_PERF_SOURCES}
    PUBLIC_HEADERS ${CYBER_PERF_HEADERS}
    DEPENDS core platform
)

cyber_add_test(
    NAME perf_profiler
    SOURCES tests/profiler_test.cpp
    DEPENDS perf
)

cyber_add_test(
    NAME perf_public_headers
    SOURCES tests/public_headers_test.cpp
    DEPENDS perf
)
