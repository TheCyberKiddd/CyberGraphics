#include <cyber/platform/time.h>
#include <cyber/software/frame_pacer.h>
#include <cyber/test/test.h>

using namespace cyber::software;

namespace
{

constexpr f64 kTolerance = 1.0e-9;

}  // namespace

CG_TEST(software_frame_pacer_defaults)
{
    FramePacer pacer(60);
    CG_EXPECT_EQ(pacer.TargetFramesPerSecond(), u32{60});
    CG_EXPECT_NEAR(pacer.TargetFrameSeconds(), 1.0 / 60.0, kTolerance);
    CG_EXPECT_EQ(pacer.FrameCount(), u64{0});

    FramePacer clampedLow(0);
    CG_EXPECT_EQ(clampedLow.TargetFramesPerSecond(), u32{1});

    FramePacer clampedHigh(5000);
    CG_EXPECT_EQ(clampedHigh.TargetFramesPerSecond(), u32{1000});
}

CG_TEST(software_frame_pacer_plans_deadlines)
{
    FramePacer pacer(60);
    pacer.Reset(100.0);

    CG_EXPECT_NEAR(pacer.NextDeadlineSeconds(), 100.0 + 1.0 / 60.0, kTolerance);
    CG_EXPECT_NEAR(pacer.SecondsUntilNextFrame(100.0), 1.0 / 60.0, kTolerance);
    CG_EXPECT_NEAR(pacer.SecondsUntilNextFrame(100.01), 1.0 / 60.0 - 0.01, 1.0e-8);
    CG_EXPECT_EQ(pacer.SecondsUntilNextFrame(101.0), 0.0);
    CG_EXPECT_EQ(pacer.SecondsUntilNextFrame(1.0e9), 0.0);
}

CG_TEST(software_frame_pacer_advances_on_grid)
{
    FramePacer pacer(60);
    pacer.Reset(0.0);
    const f64 period = pacer.TargetFrameSeconds();

    pacer.EndFrame(0.0);
    CG_EXPECT_EQ(pacer.FrameCount(), u64{1});
    CG_EXPECT_NEAR(pacer.NextDeadlineSeconds(), 2.0 * period, kTolerance);

    // A frame that is late by a small amount still keeps the fixed grid.
    pacer.EndFrame(period + 0.001);
    CG_EXPECT_NEAR(pacer.NextDeadlineSeconds(), 3.0 * period, kTolerance);
    CG_EXPECT_NEAR(pacer.LastFrameSeconds(), period + 0.001, 1.0e-6);

    pacer.EndFrame(3.0 * period - 0.001);
    CG_EXPECT_NEAR(pacer.NextDeadlineSeconds(), 4.0 * period, kTolerance);
}

CG_TEST(software_frame_pacer_resyncs_after_stall)
{
    FramePacer pacer(60, 2);
    pacer.Reset(0.0);
    const f64 period = pacer.TargetFrameSeconds();

    pacer.EndFrame(0.0);
    // More than two periods late: drop the backlog instead of catching up.
    const f64 stalledAt = 10.0;
    pacer.EndFrame(stalledAt);
    CG_EXPECT_NEAR(pacer.NextDeadlineSeconds(), stalledAt + period, kTolerance);

    // A stall within the catch-up budget still advances one slot from the
    // deadline that EndFrame(0.0) already moved forward.
    FramePacer patient(60, 4);
    patient.Reset(0.0);
    patient.EndFrame(0.0);
    patient.EndFrame(3.0 * period);
    CG_EXPECT_NEAR(patient.NextDeadlineSeconds(), 3.0 * period, kTolerance);
}

CG_TEST(software_frame_pacer_statistics)
{
    FramePacer pacer(60);
    pacer.Reset(0.0);
    const f64 period = pacer.TargetFrameSeconds();

    pacer.EndFrame(period);
    CG_EXPECT_NEAR(pacer.LastFrameSeconds(), period, kTolerance);
    CG_EXPECT_NEAR(pacer.SmoothedFrameSeconds(), period, 1.0e-6);
    CG_EXPECT_NEAR(pacer.SmoothedFramesPerSecond(), 60.0, 1.0e-3);

    pacer.EndFrame(2.0 * period + 0.01);
    CG_EXPECT_TRUE(pacer.LastFrameSeconds() > period);
    CG_EXPECT_TRUE(pacer.SmoothedFrameSeconds() < pacer.LastFrameSeconds());
    CG_EXPECT_TRUE(pacer.SmoothedFramesPerSecond() > 0.0);

    // Negative or unset times are clamped to zero, never accumulate backwards.
    pacer.EndFrame(0.0);
    CG_EXPECT_EQ(pacer.LastFrameSeconds(), 0.0);
    pacer.Reset(-1.0);
    CG_EXPECT_EQ(pacer.FrameCount(), u64{0});
}

CG_TEST(software_frame_pacer_waits_in_real_time)
{
    const f64 start = cyber::platform::Clock::NowSeconds();
    FramePacer pacer(200);
    const f64 period = pacer.TargetFrameSeconds();
    for (int frame = 0; frame < 5; ++frame)
    {
        const f64 delta = pacer.Wait();
        CG_EXPECT_TRUE(delta > 0.0);
    }
    const f64 elapsed = cyber::platform::Clock::NowSeconds() - start;
    CG_EXPECT_EQ(pacer.FrameCount(), u64{5});
    // Four sleeps of 5 ms: allow scheduler overshoot but not early returns.
    CG_EXPECT_TRUE(elapsed >= 4.0 * period - 0.002);
    CG_EXPECT_TRUE(elapsed < 2.0);
}
