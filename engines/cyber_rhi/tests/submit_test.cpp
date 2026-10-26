#include "rhi_fixture.h"

#include <cyber/memory/span.h>
#include <cyber/rhi/submission.h>
#include <cyber/test/test.h>

#include <utility>

using namespace cyber::rhi;
using namespace cyber::rhi::test;
using cyber::core::ErrorCode;
using cyber::core::u32;
using cyber::core::u64;
using cyber::memory::Span;

namespace
{

CommandListHandle FakeList(u32 index) noexcept
{
    CommandListHandle handle;
    handle.index = index;
    handle.generation = 1;
    return handle;
}

bool MakeEndedList(Fixture& fixture, CommandList* out,
                   QueueKind kind = QueueKind::Graphics) noexcept
{
    CommandListDesc desc{};
    desc.queue = kind;
    auto created = fixture.device.CreateCommandList(desc);
    if (!created)
    {
        return false;
    }
    *out = std::move(created.Value());
    if (!(*out).Begin() || !(*out).End())
    {
        return false;
    }
    return true;
}

}  // namespace

CG_TEST(rhi_submit_description_rules)
{
    // A submission with nothing to do is a caller bug, not an empty frame.
    const auto empty = ValidateSubmitDesc(SubmitDesc{});
    CG_EXPECT_FALSE(empty);
    if (!empty)
    {
        CG_EXPECT_EQ(empty.Error().code, ErrorCode::InvalidArgument);
    }

    const CommandListHandle list = FakeList(1);
    SubmitDesc desc{};
    desc.commandLists = Span<const CommandListHandle>(&list, 1);
    CG_EXPECT_TRUE(ValidateSubmitDesc(desc).IsOk());

    // A queue class that is not a class.
    SubmitDesc badQueue = desc;
    badQueue.queue = QueueKind::Count;
    CG_EXPECT_FALSE(ValidateSubmitDesc(badQueue));

    // A default handle names no list.
    const CommandListHandle invalid;
    SubmitDesc invalidList{};
    invalidList.commandLists = Span<const CommandListHandle>(&invalid, 1);
    CG_EXPECT_FALSE(ValidateSubmitDesc(invalidList));

    // Running one list twice in one batch is refused.
    const CommandListHandle twice[2] = {list, list};
    SubmitDesc repeated{};
    repeated.commandLists = Span<const CommandListHandle>(twice, 2);
    CG_EXPECT_FALSE(ValidateSubmitDesc(repeated));

    // One fence cannot be signalled two different values by one batch, because
    // the order the queue reaches them is undefined.
    const FenceSignal conflicting[2] = {{FenceHandle{1, 1}, 1}, {FenceHandle{1, 1}, 2}};
    SubmitDesc signals{};
    signals.commandLists = Span<const CommandListHandle>(&list, 1);
    signals.signals = Span<const FenceSignal>(conflicting, 2);
    CG_EXPECT_FALSE(ValidateSubmitDesc(signals));

    // The same value twice is harmless.
    const FenceSignal repeatedSignal[2] = {{FenceHandle{1, 1}, 3}, {FenceHandle{1, 1}, 3}};
    signals.signals = Span<const FenceSignal>(repeatedSignal, 2);
    CG_EXPECT_TRUE(ValidateSubmitDesc(signals).IsOk());

    // Waiting on a value the same batch is about to signal is a deadlock
    // expressed as data.
    const GpuWait selfWait{FenceHandle{2, 1}, 4};
    const FenceSignal selfSignal{FenceHandle{2, 1}, 4};
    SubmitDesc deadlock{};
    deadlock.commandLists = Span<const CommandListHandle>(&list, 1);
    deadlock.gpuWaits = Span<const GpuWait>(&selfWait, 1);
    deadlock.signals = Span<const FenceSignal>(&selfSignal, 1);
    CG_EXPECT_FALSE(ValidateSubmitDesc(deadlock));

    // Too many lists for one batch.
    CommandListHandle many[kMaxCommandListsPerSubmit + 1];
    for (u32 index = 0; index < kMaxCommandListsPerSubmit + 1; ++index)
    {
        many[index] = FakeList(index + 1);
    }
    SubmitDesc crowded{};
    crowded.commandLists = Span<const CommandListHandle>(many, kMaxCommandListsPerSubmit + 1);
    const auto overflow = ValidateSubmitDesc(crowded);
    CG_EXPECT_FALSE(overflow);
    if (!overflow)
    {
        CG_EXPECT_EQ(overflow.Error().code, ErrorCode::OutOfRange);
    }
}

CG_TEST(rhi_submit_signals_and_reports)
{
    Fixture fixture;
    CG_REQUIRE(fixture.Build());

    CommandList list;
    CG_REQUIRE(MakeEndedList(fixture, &list));

    const CommandListHandle handles[1] = {list.Handle()};
    const FenceSignal signal{fixture.fence, 7};
    SubmitDesc desc{};
    desc.queue = QueueKind::Graphics;
    desc.commandLists = Span<const CommandListHandle>(handles, 1);
    desc.signals = Span<const FenceSignal>(&signal, 1);
    desc.label = "frame 0";
    CG_REQUIRE(fixture.device.Submit(desc).IsOk());

    CG_EXPECT_EQ(fixture.device.FenceValue(fixture.fence), u64{7});
    const SubmissionInfo info = fixture.device.LastSubmission(QueueKind::Graphics);
    CG_EXPECT_TRUE(info.valid);
    CG_EXPECT_EQ(info.commandListCount, u32{1});
    CG_EXPECT_EQ(info.signalledValue, u64{7});
    CG_EXPECT_EQ(info.queue, QueueKind::Graphics);

    // A queue the device has no submission on reports nothing rather than
    // pretending the graphics submission happened there.
    CG_EXPECT_FALSE(fixture.device.LastSubmission(QueueKind::Compute).valid);

    // A batch of two lists runs as one submission.
    CommandList first;
    CommandList second;
    CG_REQUIRE(MakeEndedList(fixture, &first));
    CG_REQUIRE(MakeEndedList(fixture, &second));
    const CommandListHandle pair[2] = {first.Handle(), second.Handle()};
    const FenceSignal next{fixture.fence, 8};
    SubmitDesc batch{};
    batch.commandLists = Span<const CommandListHandle>(pair, 2);
    batch.signals = Span<const FenceSignal>(&next, 1);
    CG_REQUIRE(fixture.device.Submit(batch).IsOk());
    CG_EXPECT_EQ(fixture.device.FenceValue(fixture.fence), u64{8});
    CG_EXPECT_EQ(fixture.device.LastSubmission(QueueKind::Graphics).commandListCount, u32{2});
}

CG_TEST(rhi_submit_rejects_lists_that_are_not_ready)
{
    Fixture fixture;
    CG_REQUIRE(fixture.Build());

    CommandList recording;
    {
        auto created = fixture.device.CreateCommandList(CommandListDesc{});
        CG_REQUIRE(static_cast<bool>(created));
        recording = std::move(created.Value());
        CG_REQUIRE(recording.Begin().IsOk());
    }

    const CommandListHandle recordingHandle[1] = {recording.Handle()};
    SubmitDesc desc{};
    desc.commandLists = Span<const CommandListHandle>(recordingHandle, 1);
    const auto stillRecording = fixture.device.Submit(desc);
    CG_EXPECT_FALSE(stillRecording);
    if (!stillRecording)
    {
        CG_EXPECT_EQ(stillRecording.Error().code, ErrorCode::InvalidArgument);
    }

    // A list that was never begun has nothing to run.
    CommandList fresh;
    {
        auto created = fixture.device.CreateCommandList(CommandListDesc{});
        CG_REQUIRE(static_cast<bool>(created));
        fresh = std::move(created.Value());
    }
    const CommandListHandle freshHandle[1] = {fresh.Handle()};
    desc.commandLists = Span<const CommandListHandle>(freshHandle, 1);
    CG_EXPECT_FALSE(fixture.device.Submit(desc));

    CG_REQUIRE(recording.End().IsOk());
    desc.commandLists = Span<const CommandListHandle>(recordingHandle, 1);

    // A list recorded for one queue class does not run on another.
    desc.queue = QueueKind::Copy;
    const auto wrongQueue = fixture.device.Submit(desc);
    CG_EXPECT_FALSE(wrongQueue);
    desc.queue = QueueKind::Graphics;
    CG_EXPECT_TRUE(fixture.device.Submit(desc).IsOk());

    // A handle whose list is gone is reported, never resolved into a new list.
    CommandListHandle stale;
    {
        auto created = fixture.device.CreateCommandList(CommandListDesc{});
        CG_REQUIRE(static_cast<bool>(created));
        CommandList temporary = std::move(created.Value());
        CG_REQUIRE(temporary.Begin().IsOk());
        CG_REQUIRE(temporary.End().IsOk());
        stale = temporary.Handle();
    }
    const CommandListHandle staleSpan[1] = {stale};
    desc.commandLists = Span<const CommandListHandle>(staleSpan, 1);
    const auto gone = fixture.device.Submit(desc);
    CG_EXPECT_FALSE(gone);
    if (!gone)
    {
        CG_EXPECT_EQ(gone.Error().code, ErrorCode::InvalidArgument);
    }
}

CG_TEST(rhi_submit_gpu_waits)
{
    Fixture fixture;
    CG_REQUIRE(fixture.Build());

    auto other = fixture.device.CreateFence(FenceDesc{});
    CG_REQUIRE(static_cast<bool>(other));
    const FenceHandle otherFence = other.Value();

    CommandList list;
    CG_REQUIRE(MakeEndedList(fixture, &list));
    const CommandListHandle handles[1] = {list.Handle()};

    // A wait on a value no submission has reached cannot be satisfied, and the
    // null backend says so instead of blocking forever on work that will never
    // arrive.
    const GpuWait wait{otherFence, 3};
    const FenceSignal signal{fixture.fence, 1};
    SubmitDesc desc{};
    desc.commandLists = Span<const CommandListHandle>(handles, 1);
    desc.gpuWaits = Span<const GpuWait>(&wait, 1);
    desc.signals = Span<const FenceSignal>(&signal, 1);
    const auto blocked = fixture.device.Submit(desc);
    CG_EXPECT_FALSE(blocked);
    if (!blocked)
    {
        CG_EXPECT_EQ(blocked.Error().code, ErrorCode::NotFound);
    }
    // The failed submission moved nothing.
    CG_EXPECT_EQ(fixture.device.FenceValue(fixture.fence), u64{0});

    CG_REQUIRE(fixture.device.SignalFence(otherFence, 3).IsOk());
    CG_EXPECT_TRUE(fixture.device.Submit(desc).IsOk());
    CG_EXPECT_EQ(fixture.device.FenceValue(fixture.fence), u64{1});
    CG_EXPECT_EQ(fixture.device.LastSubmission(QueueKind::Graphics).signalledValue, u64{1});

    // A stale fence is refused whether it is waited on or signalled.
    CG_REQUIRE(fixture.device.DestroyFence(otherFence).IsOk());
    const auto staleWait = fixture.device.Submit(desc);
    CG_EXPECT_FALSE(staleWait);
    if (!staleWait)
    {
        CG_EXPECT_EQ(staleWait.Error().code, ErrorCode::InvalidArgument);
    }
}

CG_TEST(rhi_submit_on_a_device_with_no_submissions)
{
    // An empty device reports nothing and accepts nothing, rather than reading
    // through a null interface.
    Device device;
    CG_EXPECT_FALSE(device.LastSubmission(QueueKind::Graphics).valid);
    const CommandListHandle handle = FakeList(1);
    SubmitDesc desc{};
    desc.commandLists = Span<const CommandListHandle>(&handle, 1);
    CG_EXPECT_FALSE(device.Submit(desc));
}
