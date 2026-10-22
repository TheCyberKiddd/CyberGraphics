#include <cyber/platform/time.h>
#include <cyber/test/test.h>

#include <chrono>
#include <thread>

using namespace cyber::platform;

namespace
{

void SleepMilliseconds(int milliseconds)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

}  // namespace

CG_TEST(platform_clock_is_monotonic)
{
    Clock clock;
    const u64 first = clock.Ticks();
    SleepMilliseconds(5);
    const u64 second = clock.Ticks();

    CG_EXPECT_TRUE(second >= first);
    CG_EXPECT_TRUE(clock.Seconds() >= 0.0);
    CG_EXPECT_TRUE(Clock::NowSeconds() > 0.0);
}

CG_TEST(platform_frame_timer_clamps_and_counts)
{
    FrameTimer timer(0.001);
    CG_EXPECT_EQ(timer.Tick(), 0.0);

    SleepMilliseconds(10);
    const f64 delta = timer.Tick();
    CG_EXPECT_TRUE(delta > 0.0);
    CG_EXPECT_TRUE(delta <= 0.001);
    CG_EXPECT_EQ(timer.FrameCount(), u64{2});
    CG_EXPECT_TRUE(timer.TotalSeconds() > 0.0);

    timer.Reset();
    CG_EXPECT_EQ(timer.FrameCount(), u64{0});
    CG_EXPECT_EQ(timer.DeltaSeconds(), 0.0);
}

CG_TEST(platform_fixed_timestep_accumulates)
{
    FixedTimestep timestep(1.0 / 60.0, 4);
    CG_EXPECT_EQ(timestep.Advance(0.0), u32{0});
    CG_EXPECT_EQ(timestep.Advance(1.0 / 60.0), u32{1});
    CG_EXPECT_TRUE(timestep.Accumulator() >= 0.0);
    CG_EXPECT_TRUE(timestep.Alpha() >= 0.0);

    const u32 steps = timestep.Advance(1.0);
    CG_EXPECT_EQ(steps, u32{4});
    CG_EXPECT_TRUE(timestep.Accumulator() >= 0.0);
    CG_EXPECT_TRUE(timestep.Accumulator() < timestep.StepSeconds());

    timestep.Reset();
    CG_EXPECT_EQ(timestep.Accumulator(), 0.0);
}

CG_TEST(platform_sleep_respects_deadline)
{
    const f64 start = Clock::NowSeconds();
    SleepUntilSeconds(start + 0.005);
    const f64 elapsed = Clock::NowSeconds() - start;
    CG_EXPECT_TRUE(elapsed >= 0.004);
    CG_EXPECT_TRUE(elapsed < 2.0);

    // A deadline in the past returns without sleeping.
    const f64 pastStart = Clock::NowSeconds();
    SleepUntilSeconds(pastStart - 1.0);
    CG_EXPECT_TRUE(Clock::NowSeconds() - pastStart < 0.2);

    // SleepSeconds is a relative form of the same primitive.
    const f64 relativeStart = Clock::NowSeconds();
    SleepSeconds(0.002);
    CG_EXPECT_TRUE(Clock::NowSeconds() - relativeStart >= 0.0015);

    const f64 noOpStart = Clock::NowSeconds();
    SleepSeconds(-1.0);
    SleepSeconds(0.0);
    CG_EXPECT_TRUE(Clock::NowSeconds() - noOpStart < 0.2);
}
