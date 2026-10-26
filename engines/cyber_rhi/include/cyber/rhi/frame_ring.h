#pragma once

#include <cyber/core/error.h>
#include <cyber/core/result.h>
#include <cyber/core/types.h>
#include <cyber/rhi/descriptors.h>
#include <cyber/rhi/handle.h>

namespace cyber::rhi
{

using cyber::core::Error;
using cyber::core::Result;
using cyber::core::u32;
using cyber::core::u64;

class Device;

// Default number of frames the CPU may record ahead of the GPU. Three is the
// usual choice: enough to hide submission cost, few enough that input latency
// stays under one refresh.
inline constexpr u32 kDefaultFramesInFlight = 3;

// Most frames a ring may track. Four already costs more latency than it buys
// throughput on every target this project supports.
inline constexpr u32 kMaxFramesInFlight = 4;

// Description of a frame ring.
struct FrameRingDesc
{
    // Timeline fence the ring signals. The caller creates it, typically at 0,
    // and shares it with the submission that ends each frame. Must be a live
    // handle.
    FenceHandle fence;
    // Frames the CPU may have in flight. One to `kMaxFramesInFlight`.
    u32 framesInFlight = kDefaultFramesInFlight;
};

// One frame the caller may now record.
struct FrameSlot
{
    // False when the ring has no slot to give; every other field is zero.
    bool valid = false;
    // Slot index in the ring, from 0 to `framesInFlight - 1`. A frame's
    // per-slot resources are indexed by it.
    u32 slot = 0;
    // Monotonic frame number, starting at 0 for the first frame the ring hands
    // out.
    u64 frameIndex = 0;
    // Value the frame's submission must signal on the ring's fence. Waiting for
    // this value is how the ring knows the slot is free again.
    u64 signalValue = 0;
};

// A fixed ring of frames in flight over one timeline fence.
//
// The ring enforces the one rule that keeps a frame from overwriting resources
// the GPU is still reading: before handing out a slot, it waits until the frame
// that last used that slot has completed. With three frames in flight, frame N
// waits for frame N-3, not for frame N-1, which is what lets three frames be in
// flight at once.
//
// The ring owns no fence and destroys nothing. It is a policy object: one ring
// per frame-producing thread, and the fence it names is the same fence the
// submission signals.
class FrameRing
{
public:
    FrameRing() noexcept = default;

    // Creates a ring over `desc.fence`. Returns `ErrorCode::InvalidArgument` for
    // an invalid fence handle or a `framesInFlight` outside 1..kMaxFramesInFlight.
    // The ring starts at frame 0 with every slot free, so the first
    // `framesInFlight` calls never wait.
    [[nodiscard]] static Result<FrameRing, Error> Create(const FrameRingDesc& desc) noexcept;

    // Waits until the slot this frame will use has completed, advances the frame
    // counter, and returns the slot to record into.
    //
    // `timeoutNanoseconds` is `0` to poll and `kInfiniteWaitNanoseconds` to
    // block. Returns `ErrorCode::NotFound` when the slot's frame has not
    // completed within the timeout, which a caller must treat as "the GPU is
    // behind, do not reuse the slot" rather than as an invalid handle.
    [[nodiscard]] Result<FrameSlot, Error>
    BeginFrame(Device& device, u64 timeoutNanoseconds = kInfiniteWaitNanoseconds) noexcept;

    // True when the frame `frameIndex` signalled a value the fence has reached.
    // A frame index the ring has not handed out yet is reported as incomplete.
    [[nodiscard]] bool IsFrameComplete(const Device& device, u64 frameIndex) const noexcept;

    // True when the ring has bound a fence and a slot count.
    [[nodiscard]] bool IsValid() const noexcept;

    // Fence the ring signals.
    [[nodiscard]] FenceHandle Fence() const noexcept;

    // Frames the ring allows in flight.
    [[nodiscard]] u32 FramesInFlight() const noexcept;

    // Slot the next frame will use.
    [[nodiscard]] u32 NextSlot() const noexcept;

    // Number of frames handed out so far.
    [[nodiscard]] u64 FrameIndex() const noexcept;

    // Value a frame's submission must signal, or 0 for a frame index the ring
    // has not handed out.
    [[nodiscard]] u64 SignalValueForFrame(u64 frameIndex) const noexcept;

    // Returns the ring to frame 0 without touching the fence. A caller that
    // resets a ring must have waited for the device to go idle first, or the
    // slots it is about to reuse may still be in use.
    void Reset() noexcept;

private:
    FenceHandle m_fence;
    u32 m_framesInFlight = 0;
    u64 m_frameIndex = 0;
};

}  // namespace cyber::rhi
