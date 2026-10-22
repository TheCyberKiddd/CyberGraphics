#include <cyber/perf/profiler.h>
#include <cyber/test/test.h>

using namespace cyber::perf;
using cyber::core::u32;

namespace
{

class RecordingProfiler final : public Profiler
{
public:
    void BeginScope(const char* name) noexcept override
    {
        lastName = name;
        ++begins;
    }

    void EndScope() noexcept override
    {
        ++ends;
    }

    void Counter(const char* name, f64 value) noexcept override
    {
        lastName = name;
        lastValue = value;
        ++counters;
    }

    const char* lastName = nullptr;
    f64 lastValue = 0.0;
    u32 begins = 0;
    u32 ends = 0;
    u32 counters = 0;
};

}  // namespace

CG_TEST(perf_null_profiler_is_noop)
{
    NullProfiler& first = NullProfilerInstance();
    NullProfiler& second = NullProfilerInstance();
    CG_EXPECT_TRUE(&first == &second);

    first.BeginScope("scope");
    first.EndScope();
    first.Counter("counter", 1.0);
}

CG_TEST(perf_custom_profiler_receives_events)
{
    RecordingProfiler profiler;
    profiler.BeginScope("frame");
    profiler.EndScope();
    profiler.Counter("draws", 12.0);

    CG_EXPECT_EQ(profiler.begins, u32{1});
    CG_EXPECT_EQ(profiler.ends, u32{1});
    CG_EXPECT_EQ(profiler.counters, u32{1});
    CG_EXPECT_STREQ(profiler.lastName, "draws");
    CG_EXPECT_EQ(profiler.lastValue, 12.0);
}
