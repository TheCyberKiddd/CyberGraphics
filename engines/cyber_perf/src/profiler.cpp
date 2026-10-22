#include <cyber/perf/profiler.h>

namespace cyber::perf
{

void NullProfiler::BeginScope(const char* name) noexcept
{
    (void)name;
}

void NullProfiler::EndScope() noexcept {}

void NullProfiler::Counter(const char* name, f64 value) noexcept
{
    (void)name;
    (void)value;
}

NullProfiler& NullProfilerInstance() noexcept
{
    static NullProfiler instance;
    return instance;
}

}  // namespace cyber::perf
