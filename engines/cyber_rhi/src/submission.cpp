// Submission validation that needs no backend.
//
// A submission is the one call a render thread makes per queue per frame, so its
// descriptor is checked for everything the RHI can see — the queue class, the
// list count, duplicates, and the fence tables — and nothing more. A stale list
// or a list recorded for another queue is the backend's check, because only the
// backend can resolve a handle's generation.

#include <cyber/rhi/submission.h>

namespace cyber::rhi
{

namespace
{

using cyber::core::ErrorCode;
using cyber::core::usize;

[[nodiscard]] Status<Error> Fail(ErrorCode code, const char* message) noexcept
{
    return Status<Error>::Err(Error{code, message});
}

}  // namespace

Status<Error> ValidateSubmitDesc(const SubmitDesc& desc) noexcept
{
    if (static_cast<u32>(desc.queue) >= kQueueKindCount)
    {
        return Fail(ErrorCode::InvalidArgument, "a submission names a queue class that is not one");
    }
    if (desc.commandLists.Empty())
    {
        return Fail(ErrorCode::InvalidArgument, "a submission needs at least one command list");
    }
    if (desc.commandLists.Data() == nullptr)
    {
        return Fail(ErrorCode::InvalidArgument, "a submission's command list span is null");
    }
    if (desc.commandLists.Size() > kMaxCommandListsPerSubmit)
    {
        return Fail(ErrorCode::OutOfRange,
                    "a submission carries more command lists than kMaxCommandListsPerSubmit");
    }
    for (usize index = 0; index < desc.commandLists.Size(); ++index)
    {
        if (!desc.commandLists[index].IsValid())
        {
            return Fail(ErrorCode::InvalidArgument,
                        "a submission names a command list with an invalid handle");
        }
        // Running the same list twice in one batch is legal in D3D12 but almost
        // always a mistake a caller wants to hear about; the second run sees the
        // state the first left behind.
        for (usize other = 0; other < index; ++other)
        {
            if (desc.commandLists[other] == desc.commandLists[index])
            {
                return Fail(ErrorCode::InvalidArgument, "a submission repeats a command list");
            }
        }
    }

    if (desc.signals.Size() > kMaxSignalsPerSubmit)
    {
        return Fail(ErrorCode::OutOfRange,
                    "a submission signals more fences than kMaxSignalsPerSubmit");
    }
    for (usize index = 0; index < desc.signals.Size(); ++index)
    {
        const FenceSignal& signal = desc.signals[index];
        if (!signal.fence.IsValid())
        {
            return Fail(ErrorCode::InvalidArgument,
                        "a submission signals a fence with an invalid handle");
        }
        for (usize other = 0; other < index; ++other)
        {
            if (desc.signals[other].fence == signal.fence &&
                desc.signals[other].value != signal.value)
            {
                return Fail(ErrorCode::InvalidArgument,
                            "a submission signals one fence two different values");
            }
        }
    }

    if (desc.gpuWaits.Size() > kMaxGpuWaitsPerSubmit)
    {
        return Fail(ErrorCode::OutOfRange,
                    "a submission waits on more fences than kMaxGpuWaitsPerSubmit");
    }
    for (usize index = 0; index < desc.gpuWaits.Size(); ++index)
    {
        const GpuWait& wait = desc.gpuWaits[index];
        if (!wait.fence.IsValid())
        {
            return Fail(ErrorCode::InvalidArgument,
                        "a submission waits on a fence with an invalid handle");
        }
        // A queue that waits on a fence it is about to signal with the same
        // value is a deadlock expressed as data. It is caught here rather than
        // hanging a frame, because no driver reports it usefully.
        for (usize signalIndex = 0; signalIndex < desc.signals.Size(); ++signalIndex)
        {
            if (desc.signals[signalIndex].fence == wait.fence &&
                desc.signals[signalIndex].value <= wait.value)
            {
                return Fail(ErrorCode::InvalidArgument,
                            "a submission waits on a value it is about to signal");
            }
        }
    }
    return Status<Error>::Ok();
}

}  // namespace cyber::rhi
