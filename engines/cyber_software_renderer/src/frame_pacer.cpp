#include <cyber/platform/time.h>
#include <cyber/software/frame_pacer.h>

namespace cyber::software
{

namespace
{

// Exponential moving average weight for the overlay readout. 0.1 settles over
// roughly 30 frames, which is readable without hiding a frame spike for long.
constexpr f64 kSmoothingWeight = 0.1;

// Windows rounds a timer sleep up to its scheduling quantum (about 1 ms), so a
// sleep that ends exactly on the deadline lands late and the frame rate drifts
// below the target. Wait sleeps to just before the deadline and spins out the
// final fraction against the steady clock.
constexpr f64 kSpinMarginSeconds = 0.0015;

f64 SanitizeNow(f64 nowSeconds) noexcept
{
    if (!(nowSeconds >= 0.0))
    {
        return platform::Clock::NowSeconds();
    }
    return nowSeconds;
}

}  // namespace

FramePacer::FramePacer(u32 targetFramesPerSecond, u32 maxCatchUpFrames) noexcept
    : m_maxCatchUpFrames(maxCatchUpFrames == 0 ? 1 : maxCatchUpFrames)
{
    u32 target = targetFramesPerSecond;
    if (target < 1)
    {
        target = 1;
    }
    else if (target > 1000)
    {
        target = 1000;
    }
    m_periodSeconds = 1.0 / static_cast<f64>(target);
    m_smoothedFrameSeconds = m_periodSeconds;
    Reset(platform::Clock::NowSeconds());
}

void FramePacer::Reset(f64 nowSeconds) noexcept
{
    const f64 now = SanitizeNow(nowSeconds);
    m_nextDeadlineSeconds = now + m_periodSeconds;
    m_lastEndSeconds = now;
    m_lastFrameSeconds = 0.0;
    m_smoothedFrameSeconds = m_periodSeconds;
    m_frameCount = 0;
}

u32 FramePacer::TargetFramesPerSecond() const noexcept
{
    if (!(m_periodSeconds > 0.0))
    {
        return 0;
    }
    const f64 frames = 1.0 / m_periodSeconds;
    if (frames >= 1000.0)
    {
        return 1000;
    }
    return static_cast<u32>(frames + 0.5);
}

f64 FramePacer::TargetFrameSeconds() const noexcept
{
    return m_periodSeconds;
}

f64 FramePacer::NextDeadlineSeconds() const noexcept
{
    return m_nextDeadlineSeconds;
}

f64 FramePacer::SecondsUntilNextFrame(f64 nowSeconds) const noexcept
{
    const f64 remaining = m_nextDeadlineSeconds - SanitizeNow(nowSeconds);
    return remaining > 0.0 ? remaining : 0.0;
}

void FramePacer::EndFrame(f64 nowSeconds) noexcept
{
    const f64 now = SanitizeNow(nowSeconds);
    f64 delta = now - m_lastEndSeconds;
    if (!(delta >= 0.0))
    {
        delta = 0.0;
    }

    m_lastFrameSeconds = delta;
    if (m_frameCount == 0)
    {
        m_smoothedFrameSeconds = delta > 0.0 ? delta : m_periodSeconds;
    }
    else
    {
        m_smoothedFrameSeconds += kSmoothingWeight * (delta - m_smoothedFrameSeconds);
    }
    m_lastEndSeconds = now;
    ++m_frameCount;

    const f64 catchUpSeconds = m_periodSeconds * static_cast<f64>(m_maxCatchUpFrames);
    if (now > m_nextDeadlineSeconds + catchUpSeconds)
    {
        // A long stall (debugger, window drag) must not queue a burst of frames.
        m_nextDeadlineSeconds = now + m_periodSeconds;
    }
    else
    {
        m_nextDeadlineSeconds += m_periodSeconds;
    }
}

f64 FramePacer::Wait() noexcept
{
    f64 now = platform::Clock::NowSeconds();
    const f64 remaining = SecondsUntilNextFrame(now);
    if (remaining > kSpinMarginSeconds)
    {
        platform::SleepUntilSeconds(now + remaining - kSpinMarginSeconds);
        now = platform::Clock::NowSeconds();
    }
    // A late frame has no remaining time and returns without spinning.
    while (now < m_nextDeadlineSeconds)
    {
        now = platform::Clock::NowSeconds();
    }
    EndFrame(now);
    return m_lastFrameSeconds;
}

f64 FramePacer::LastFrameSeconds() const noexcept
{
    return m_lastFrameSeconds;
}

f64 FramePacer::SmoothedFrameSeconds() const noexcept
{
    return m_smoothedFrameSeconds;
}

f64 FramePacer::SmoothedFramesPerSecond() const noexcept
{
    if (!(m_smoothedFrameSeconds > 0.0))
    {
        return 0.0;
    }
    return 1.0 / m_smoothedFrameSeconds;
}

u64 FramePacer::FrameCount() const noexcept
{
    return m_frameCount;
}

}  // namespace cyber::software
