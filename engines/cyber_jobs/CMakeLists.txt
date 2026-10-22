set(CYBER_JOBS_SOURCES
    src/job_system.cpp
)

set(CYBER_JOBS_HEADERS
    include/cyber/jobs/job_system.h
    include/cyber/jobs/jobs.h
)

cyber_add_module(
    NAME jobs
    LAYER 1
    SOURCES ${CYBER_JOBS_SOURCES}
    PUBLIC_HEADERS ${CYBER_JOBS_HEADERS}
    DEPENDS core memory platform perf
)

cyber_add_test(
    NAME jobs_job_system
    SOURCES tests/job_system_test.cpp
    DEPENDS jobs memory
)

cyber_add_test(
    NAME jobs_public_headers
    SOURCES tests/public_headers_test.cpp
    DEPENDS jobs
)
