#pragma once

#include <cyber/core/error.h>
#include <cyber/core/result.h>
#include <cyber/core/types.h>
#include <cyber/memory/span.h>
#include <cyber/rhi/descriptors.h>
#include <cyber/rhi/handle.h>

namespace cyber::rhi
{

using cyber::core::Error;
using cyber::core::Status;
using cyber::core::u32;
using cyber::core::u64;

// A fence value a submission makes the queue reach.
//
// `value` must be at or above the fence's current value; a signal below it is
// accepted and ignored, because a timeline never moves backwards. A submission
// with several signals signals them all when the work finishes, which is how one
// frame's completion wakes a frame ring and a swapchain at once.
struct FenceSignal
{
    FenceHandle fence;
    u64 value = 0;
};

// A fence value a queue must not pass before it begins the submission.
//
// This is a GPU-side wait: the CPU hands the work over and continues, and the
// queue itself blocks until the named timeline reaches the value. It is D3D12's
// `ID3D12CommandQueue::Wait` and Vulkan's timeline semaphore wait, and it is the
// only correct way to order work between two queues, because a CPU wait would
// stall the frame for the GPU.
struct GpuWait
{
    FenceHandle fence;
    u64 value = 0;
};

// One batch of work handed to one queue.
//
// Everything in it is plain data and nothing is allocated: a renderer builds a
// `SubmitDesc` per queue per frame from storage it already owns. The lists run
// in the order given, and the signals are reached after the last of them
// finishes.
struct SubmitDesc
{
    // Queue the batch runs on. Every list must have been recorded for this
    // class; a mismatch is invalid, because command list types are not
    // interchangeable in D3D12 and Vulkan's queues are not either.
    QueueKind queue = QueueKind::Graphics;
    // Lists to run, in order. At least one, and each must have ended.
    memory::Span<const CommandListHandle> commandLists{};
    // Fence values the queue reaches when the batch finishes.
    memory::Span<const FenceSignal> signals{};
    // Fence values the queue waits for before it begins.
    memory::Span<const GpuWait> gpuWaits{};
    // Optional name for logs, debuggers, and GPU markers. Borrowed.
    const char* label = nullptr;
};

// What the backend reports about one submission. `commandListCount` and
// `barrierCount` are what it accepted; a backend that merges or splits work
// reports the merge, not the caller's view of it.
struct SubmissionInfo
{
    bool valid = false;
    QueueKind queue = QueueKind::Graphics;
    u32 commandListCount = 0;
    u32 barrierCount = 0;
    // Highest fence value the submission signalled, or 0 when it signalled
    // nothing.
    u64 signalledValue = 0;
};

// Checks the parts of a submission the RHI can check without the backend: a
// real queue class, at least one list, no duplicate list, signals and waits that
// name live-shaped values, and no duplicate signal on one fence with conflicting
// values.
//
// A stale list handle, a list recorded for another queue, and a list that has
// not ended are the backend's checks, because only it can resolve the handles.
// Returns `ErrorCode::InvalidArgument` with a message naming the rule.
[[nodiscard]] Status<Error> ValidateSubmitDesc(const SubmitDesc& desc) noexcept;

// Largest number of command lists one submission may carry. The limit exists so
// a backend can keep a fixed-size array of native handles per submission
// instead of allocating on the submit path.
inline constexpr u32 kMaxCommandListsPerSubmit = 64;

// Largest number of fence signals one submission may carry.
inline constexpr u32 kMaxSignalsPerSubmit = 8;

// Largest number of GPU-side waits one submission may carry.
inline constexpr u32 kMaxGpuWaitsPerSubmit = 8;

}  // namespace cyber::rhi
