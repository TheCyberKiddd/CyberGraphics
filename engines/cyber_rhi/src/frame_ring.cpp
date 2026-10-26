// The frame ring.
//
// One timeline, one counter. Frame N signals value N + 1, so before reusing the
// slot frame N used, a ring of K frames must see value N - K + 1. Waiting for
// that value and not for N - 1 is the whole point: it lets K frames be in flight
// at once instead of serialising the CPU against the GPU.

#include <cyber/rhi/device.h>
#include <cyber/rhi/frame_ring.h>

namespace cyber::rhi
{

namespace
{

using cyber::core::Error;
using cyber::core::ErrorCode;
using cyber::core::Result;

[[nodiscard]] Result<FrameRing, Error> RingError(ErrorCode code, const char* message) noexcept
{
    return Result<FrameRing, Error>::Err(Error{code, message});
}

[[nodiscard]] Result<FrameSlot, Error> SlotError(ErrorCode code, const char* message) noexcept
{
    return Result<FrameSlot, Error>::Err(Error{code, message});
}

}  // namespace

Result<FrameRing, Error> FrameRing::Create(const FrameRingDesc& desc) noexcept
{
    if (!desc.fence.IsValid())
    {
        return RingError(ErrorCode::InvalidArgument, "a frame ring needs a valid fence");
    }
    if (desc.framesInFlight == 0)
    {
        return RingError(ErrorCode::InvalidArgument, "a frame ring needs at least one frame");
    }
    if (desc.framesInFlight > kMaxFramesInFlight)
    {
        return RingError(ErrorCode::InvalidArgument,
                         "a frame ring allows more frames in flight than kMaxFramesInFlight");
    }

    FrameRing ring;
    ring.m_fence = desc.fence;
    ring.m_framesInFlight = desc.framesInFlight;
    ring.m_frameIndex = 0;
    return Result<FrameRing, Error>::Ok(ring);
}

Result<FrameSlot, Error> FrameRing::BeginFrame(Device& device, u64 timeoutNanoseconds) noexcept
{
    if (!IsValid())
    {
        return SlotError(ErrorCode::InvalidArgument, "frame ring is empty");
    }
    if (!device.IsValid())
    {
        return SlotError(ErrorCode::InvalidArgument, "frame ring needs a live device");
    }

    const u64 frameIndex = m_frameIndex;
    const u32 slot = static_cast<u32>(frameIndex % m_framesInFlight);

    // The first time a slot is used there is nothing to wait for. After that, the
    // frame that used the slot `framesInFlight` frames ago owns it until its
    // signal value is reached.
    if (frameIndex >= m_framesInFlight)
    {
        const u64 previousFrame = frameIndex - m_framesInFlight;
        const u64 requiredValue = previousFrame + 1;
        const auto waited = device.WaitFence(m_fence, requiredValue, timeoutNanoseconds);
        if (!waited)
        {
            // `NotFound` means the GPU is behind: the caller must not reuse the
            // slot. Any other code is a stale ring or fence.
            return SlotError(waited.Error().code, waited.Error().message);
        }
    }

    m_frameIndex = frameIndex + 1;

    FrameSlot frame;
    frame.valid = true;
    frame.slot = slot;
    frame.frameIndex = frameIndex;
    frame.signalValue = frameIndex + 1;
    return Result<FrameSlot, Error>::Ok(frame);
}

bool FrameRing::IsFrameComplete(const Device& device, u64 frameIndex) const noexcept
{
    if (!IsValid() || !device.IsValid() || frameIndex >= m_frameIndex)
    {
        return false;
    }
    return device.FenceValue(m_fence) >= frameIndex + 1;
}

bool FrameRing::IsValid() const noexcept
{
    return m_fence.IsValid() && m_framesInFlight > 0;
}

FenceHandle FrameRing::Fence() const noexcept
{
    return m_fence;
}

u32 FrameRing::FramesInFlight() const noexcept
{
    return m_framesInFlight;
}

u32 FrameRing::NextSlot() const noexcept
{
    if (m_framesInFlight == 0)
    {
        return 0;
    }
    return static_cast<u32>(m_frameIndex % m_framesInFlight);
}

u64 FrameRing::FrameIndex() const noexcept
{
    return m_frameIndex;
}

u64 FrameRing::SignalValueForFrame(u64 frameIndex) const noexcept
{
    // A frame that has not been handed out has no value yet; reporting one would
    // let a caller wait on a signal that will never come.
    if (frameIndex >= m_frameIndex)
    {
        return 0;
    }
    return frameIndex + 1;
}

void FrameRing::Reset() noexcept
{
    m_frameIndex = 0;
}

}  // namespace cyber::rhi
