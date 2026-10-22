#pragma once

#include <cyber/core/assert.h>
#include <cyber/platform/types.h>

namespace cyber::platform
{

// Monotonic steady clock. Ticks are nanoseconds since construction; the epoch
// is arbitrary and only differences are meaningful.
class Clock
{
public:
    Clock() noexcept;

    [[nodiscard]] u64 Ticks() const noexcept;
    [[nodiscard]] f64 Seconds() const noexcept;

    // Monotonic seconds since an arbitrary process-wide epoch. Do not mix values
    // from this function with wall-clock time.
    [[nodiscard]] static f64 NowSeconds() noexcept;

private:
    [[nodiscard]] static u64 NowNanoseconds() noexcept;

    u64 m_startNanoseconds = 0;
};

// Frame delta helper. The first Tick after Reset returns zero. Frame spikes are
// clamped to maxDelta so a debugger pause cannot create a huge simulation step.
class FrameTimer
{
public:
    explicit FrameTimer(f64 maxDeltaSeconds = 0.25) noexcept;

    void Reset() noexcept;
    void SetMaxDelta(f64 maxDeltaSeconds) noexcept;

    [[nodiscard]] f64 Tick() noexcept;
    [[nodiscard]] f64 DeltaSeconds() const noexcept;
    [[nodiscard]] f64 TotalSeconds() const noexcept;
    [[nodiscard]] u64 FrameCount() const noexcept;

private:
    Clock m_clock;
    f64 m_lastSeconds = 0.0;
    f64 m_deltaSeconds = 0.0;
    f64 m_totalSeconds = 0.0;
    f64 m_maxDeltaSeconds = 0.25;
    u64 m_frameCount = 0;
    bool m_firstTick = true;
};

// Fixed simulation step accumulator. Advance returns the number of steps to run
// and leaves the remainder available through Alpha for interpolation.
class FixedTimestep
{
public:
    explicit FixedTimestep(f64 stepSeconds, u32 maxSteps = 8) noexcept;

    void Reset() noexcept;

    [[nodiscard]] u32 Advance(f64 frameSeconds) noexcept;
    [[nodiscard]] f64 StepSeconds() const noexcept;
    [[nodiscard]] f64 Accumulator() const noexcept;
    [[nodiscard]] f64 Alpha() const noexcept;
    [[nodiscard]] u32 MaxSteps() const noexcept;

private:
    f64 m_stepSeconds = 1.0 / 60.0;
    f64 m_accumulator = 0.0;
    u32 m_maxSteps = 8;
};

// Suspends the calling thread until the monotonic clock reaches
// deadlineSeconds (the NowSeconds epoch). Returns once the deadline has passed;
// the OS may overshoot by a scheduling quantum. A deadline in the past returns
// immediately. Non-throwing and allocation-free.
void SleepUntilSeconds(f64 deadlineSeconds) noexcept;

// Suspends the calling thread for at least seconds. Non-positive or non-finite
// durations return immediately.
void SleepSeconds(f64 seconds) noexcept;

}  // namespace cyber::platform
