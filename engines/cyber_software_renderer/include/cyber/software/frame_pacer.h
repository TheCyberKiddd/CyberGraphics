#pragma once

#include <cyber/core/types.h>

namespace cyber::software
{

using cyber::core::f64;
using cyber::core::u32;
using cyber::core::u64;

// Monotonic frame pacer. Deadlines live on the platform steady clock, so a frame
// is never scheduled by accumulating float deltas.
//
// Typical loop:
//
//   FramePacer pacer(60);
//   while (running) {
//       pacer.Wait();          // sleeps until the next 60 Hz slot
//       ... render and present ...
//   }
//
// EndFrame may be driven with synthetic times in tests; Wait is the real-time
// path.
class FramePacer
{
public:
    // targetFramesPerSecond is clamped to [1, 1000]. maxCatchUpFrames is clamped
    // to at least 1 and bounds how far the deadline may fall behind before it
    // resynchronizes to now.
    explicit FramePacer(u32 targetFramesPerSecond = 60, u32 maxCatchUpFrames = 2) noexcept;

    // Restarts the deadline grid at nowSeconds and clears frame statistics.
    void Reset(f64 nowSeconds) noexcept;

    [[nodiscard]] u32 TargetFramesPerSecond() const noexcept;
    [[nodiscard]] f64 TargetFrameSeconds() const noexcept;
    [[nodiscard]] f64 NextDeadlineSeconds() const noexcept;

    // Seconds from nowSeconds to the next deadline, or 0 when overdue. Never
    // negative.
    [[nodiscard]] f64 SecondsUntilNextFrame(f64 nowSeconds) const noexcept;

    // Advances the deadline by one period. When nowSeconds is more than
    // maxCatchUpFrames periods past the deadline, the grid resynchronizes to
    // nowSeconds + period instead of queueing catch-up frames. Records the frame
    // time statistics.
    void EndFrame(f64 nowSeconds) noexcept;

    // Sleeps until the next deadline, ends the frame, and returns the wall time
    // since the previous Wait (or since construction/Reset for the first frame).
    [[nodiscard]] f64 Wait() noexcept;

    // Wall time recorded by the last EndFrame.
    [[nodiscard]] f64 LastFrameSeconds() const noexcept;
    // Exponential moving average of frame time, used by the overlay.
    [[nodiscard]] f64 SmoothedFrameSeconds() const noexcept;
    [[nodiscard]] f64 SmoothedFramesPerSecond() const noexcept;
    [[nodiscard]] u64 FrameCount() const noexcept;

private:
    f64 m_periodSeconds = 1.0 / 60.0;
    f64 m_nextDeadlineSeconds = 0.0;
    f64 m_lastFrameSeconds = 0.0;
    f64 m_smoothedFrameSeconds = 1.0 / 60.0;
    f64 m_lastEndSeconds = 0.0;
    u32 m_maxCatchUpFrames = 2;
    u64 m_frameCount = 0;
};

}  // namespace cyber::software
