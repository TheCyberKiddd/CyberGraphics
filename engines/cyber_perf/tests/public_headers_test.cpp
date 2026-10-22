#include <cyber/perf/perf.h>
#include <cyber/perf/profiler.h>
#include <cyber/test/test.h>

#include <type_traits>

using namespace cyber::perf;

CG_TEST(perf_public_headers)
{
    static_assert(!std::is_copy_constructible_v<Profiler>);
    static_assert(std::is_abstract_v<Profiler>);
    static_assert(std::is_base_of_v<Profiler, NullProfiler>);

    Profiler& profiler = NullProfilerInstance();
    profiler.BeginScope("header-check");
    profiler.EndScope();
    profiler.Counter("header-check", 1.0);

    CG_EXPECT_TRUE(&NullProfilerInstance() == &NullProfilerInstance());
}
