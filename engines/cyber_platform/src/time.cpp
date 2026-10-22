#include <cyber/platform/time.h>

#include <cmath>

namespace cyber::platform
{

Clock::Clock() noexcept : m_startNanoseconds(NowNanoseconds()) {}

u64 Clock::Ticks() const noexcept
{
    const u64 now = NowNanoseconds();
    return now >= m_startNanoseconds ? now - m_startNanoseconds : 0;
}

f64 Clock::Seconds() const noexcept
{
    return static_cast<f64>(Ticks()) * 1.0e-9;
}

f64 Clock::NowSeconds() noexcept
{
    return static_cast<f64>(NowNanoseconds()) * 1.0e-9;
}

FrameTimer::FrameTimer(f64 maxDeltaSeconds) noexcept
    : m_lastSeconds(m_clock.Seconds()), m_maxDeltaSeconds(maxDeltaSeconds)
{
    if (!(m_maxDeltaSeconds > 0.0))
    {
        m_maxDeltaSeconds = 0.25;
    }
}

void FrameTimer::Reset() noexcept
{
    m_clock = Clock();
    m_lastSeconds = 0.0;
    m_deltaSeconds = 0.0;
    m_totalSeconds = 0.0;
    m_frameCount = 0;
    m_firstTick = true;
}

void FrameTimer::SetMaxDelta(f64 maxDeltaSeconds) noexcept
{
    if (maxDeltaSeconds > 0.0)
    {
        m_maxDeltaSeconds = maxDeltaSeconds;
    }
}

f64 FrameTimer::Tick() noexcept
{
    const f64 now = m_clock.Seconds();
    f64 delta = m_firstTick ? 0.0 : now - m_lastSeconds;
    m_lastSeconds = now;
    m_firstTick = false;

    if (!(delta >= 0.0))
    {
        delta = 0.0;
    }
    if (delta > m_maxDeltaSeconds)
    {
        delta = m_maxDeltaSeconds;
    }

    m_deltaSeconds = delta;
    m_totalSeconds += delta;
    ++m_frameCount;
    return delta;
}

f64 FrameTimer::DeltaSeconds() const noexcept
{
    return m_deltaSeconds;
}

f64 FrameTimer::TotalSeconds() const noexcept
{
    return m_totalSeconds;
}

u64 FrameTimer::FrameCount() const noexcept
{
    return m_frameCount;
}

FixedTimestep::FixedTimestep(f64 stepSeconds, u32 maxSteps) noexcept
    : m_stepSeconds(stepSeconds), m_maxSteps(maxSteps == 0 ? 1 : maxSteps)
{
    CG_ASSERT(stepSeconds > 0.0);
    if (!(m_stepSeconds > 0.0))
    {
        m_stepSeconds = 1.0 / 60.0;
    }
}

void FixedTimestep::Reset() noexcept
{
    m_accumulator = 0.0;
}

u32 FixedTimestep::Advance(f64 frameSeconds) noexcept
{
    if (!(frameSeconds > 0.0))
    {
        return 0;
    }

    const f64 maxFrame = m_stepSeconds * static_cast<f64>(m_maxSteps);
    if (frameSeconds > maxFrame)
    {
        frameSeconds = maxFrame;
    }

    m_accumulator += frameSeconds;
    u32 steps = 0;
    while (m_accumulator >= m_stepSeconds && steps < m_maxSteps)
    {
        m_accumulator -= m_stepSeconds;
        ++steps;
    }

    if (std::isnan(m_accumulator) || std::isinf(m_accumulator))
    {
        m_accumulator = 0.0;
    }
    return steps;
}

f64 FixedTimestep::StepSeconds() const noexcept
{
    return m_stepSeconds;
}

f64 FixedTimestep::Accumulator() const noexcept
{
    return m_accumulator;
}

f64 FixedTimestep::Alpha() const noexcept
{
    return m_stepSeconds > 0.0 ? m_accumulator / m_stepSeconds : 0.0;
}

u32 FixedTimestep::MaxSteps() const noexcept
{
    return m_maxSteps;
}

}  // namespace cyber::platform
