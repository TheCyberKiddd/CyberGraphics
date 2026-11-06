// The Phase 15 deliberate-error list (ADR 0018, increment 3): the five mistakes
// the validation layer promises to catch, each caught with the resource named
// and each paired with the corrected call, because a test that only checked the
// failure would pass just as well if the call were broken.
//
// Recording-side errors (barriers, bindings) are the shared rule set the null
// backend performs and the wrapper forwards. Lifetime errors (destroy and
// reuse while a submission is in flight) are the wrapper's own, and need a
// backend whose submissions outlive `Submit`, so they run on D3D12 with a gate
// fence holding the GPU work back. These two tests skip when D3D12 is absent.

#include "rhi_fixture.h"

#include <cyber/core/log.h>
#include <cyber/core/log_sinks.h>
#include <cyber/core/types.h>
#include <cyber/memory/heap_allocator.h>
#include <cyber/memory/span.h>
#include <cyber/rhi/backend.h>
#include <cyber/rhi/command_list.h>
#include <cyber/rhi/d3d12.h>
#include <cyber/rhi/device.h>
#include <cyber/rhi/memory.h>
#include <cyber/rhi/resource_state.h>
#include <cyber/rhi/submission.h>
#include <cyber/test/test.h>

#include <cstdio>
#include <cstring>
#include <utility>

using namespace cyber::rhi;
using namespace cyber::rhi::test;
using cyber::core::ErrorCode;
using cyber::core::u32;
using cyber::core::u64;
using cyber::memory::Byte;
using cyber::memory::HeapAllocator;
using cyber::memory::Span;

namespace
{

bool MentionsBarriers(const cyber::core::Status<cyber::core::Error>& status) noexcept
{
    return !status && status.Error().message != nullptr &&
           std::strstr(status.Error().message, "barrier") != nullptr;
}

bool MentionsFlight(const cyber::core::Status<cyber::core::Error>& status) noexcept
{
    return !status && status.Error().message != nullptr &&
           std::strstr(status.Error().message, "in flight") != nullptr;
}

// Use the last adapter, which is WARP on the Windows CI runner. Device::Create
// installs the standard validating wrapper around the selected backend.
[[nodiscard]] bool OpenD3D12Device(Device& out, HeapAllocator& heap) noexcept
{
    const Backend* backend = D3D12Backend();
    if (backend == nullptr)
    {
        return false;
    }
    const u32 adapters = backend->enumerateAdapters(nullptr, 0);
    if (adapters == 0)
    {
        return false;
    }
    DeviceDesc desc{};
    desc.backend = backend;
    desc.adapterIndex = adapters - 1;
    auto created = Device::Create(desc, heap);
    if (!created)
    {
        return false;
    }
    out = std::move(created.Value());
    return true;
}

// The resource name reaches the caller through the wrapper's warning log on
// the `Rhi` channel rather than through a formatted `Status` message, whose
// messages are borrowed static strings across the module. The watch keeps the
// last logged lines so a test can assert the naming the phase asks for.
struct RejectionWatch
{
    u32 count = 0;
    char last[cyber::core::kMaxLogMessageLength]{};
};

void WatchRejections(void* userData, const cyber::core::LogRecord& record) noexcept
{
    auto* watch = static_cast<RejectionWatch*>(userData);
    if (record.channel != cyber::core::LogChannel::Rhi)
    {
        return;
    }
    if (std::strstr(record.message, "validating device rejected") == nullptr)
    {
        return;
    }
    watch->count += 1;
    std::snprintf(watch->last, sizeof(watch->last), "%.400s", record.message);
}

void RemoveRejectionWatch(cyber::core::CallbackSink& sink) noexcept
{
    (void)cyber::core::DefaultLogger().RemoveSink(&sink);
}

// Submits one ended list on the copy queue, held by a gate fence the test
// keeps shut. The list runs on the copy queue so the CPU-side fence signal
// that opens the gate later still reaches the device through the graphics
// queue, which the blocked copy queue has not stalled: this is the pattern the
// transient test below depends on, not a driver accident.
[[nodiscard]] bool SubmitGatedCopy(Device& device, CommandList& list, FenceHandle gate,
                                   FenceHandle done) noexcept
{
    const CommandListHandle lists[] = {list.Handle()};
    const GpuWait waits[] = {{gate, 1}};
    const FenceSignal signals[] = {{done, 1}};
    SubmitDesc submit{};
    submit.queue = QueueKind::Copy;
    submit.commandLists = Span<const CommandListHandle>(lists, 1);
    submit.gpuWaits = Span<const GpuWait>(waits, 1);
    submit.signals = Span<const FenceSignal>(signals, 1);
    return device.Submit(submit).IsOk();
}

}  // namespace

// The draw reads a uniform buffer that was bound but never transitioned, so the
// draw itself is the first command that can fail, and it names the state the
// buffer was never moved into.
CG_TEST(rhi_validation_errors_catch_a_missing_barrier)
{
    Fixture fixture;
    CG_REQUIRE(fixture.Build());

    CommandList list;
    CommandListDesc desc{};
    desc.queue = QueueKind::Graphics;
    auto created = fixture.device.CreateCommandList(desc);
    CG_REQUIRE(static_cast<bool>(created));
    list = std::move(created.Value());
    CG_REQUIRE(list.Begin().IsOk());
    CG_REQUIRE(fixture.TransitionTargets(list));
    CG_REQUIRE(fixture.BeginPass(list));
    CG_REQUIRE(list.SetPipeline(fixture.graphicsPipeline).IsOk());
    const BufferBarrier vertex{fixture.vertexBuffer, ResourceState::Undefined,
                               ResourceState::VertexBuffer};
    CG_REQUIRE(
        list.Barrier(Span<const BufferBarrier>(&vertex, 1), Span<const TextureBarrier>{}).IsOk());
    CG_REQUIRE(list.SetVertexBuffer(0, fixture.vertexBuffer).IsOk());
    CG_REQUIRE(list.SetBindGroup(BindGroupSlot::PerMaterial, fixture.group).IsOk());

    const auto missing = list.Draw(DrawArguments{3, 1, 0, 0});
    CG_EXPECT_FALSE(missing);
    if (!missing)
    {
        CG_EXPECT_EQ(missing.Error().code, ErrorCode::InvalidArgument);
        CG_EXPECT_TRUE(MentionsBarriers(missing));
    }

    const BufferBarrier uniform{fixture.uniformBuffer, ResourceState::Undefined,
                                ResourceState::ConstantBuffer};
    CG_REQUIRE(
        list.Barrier(Span<const BufferBarrier>(&uniform, 1), Span<const TextureBarrier>{}).IsOk());
    CG_EXPECT_TRUE(list.Draw(DrawArguments{3, 1, 0, 0}).IsOk());
    CG_EXPECT_EQ(list.Stats().draws, u32{1});
}

// The second barrier claims the buffer is in copy_source, but this list just
// recorded it as a vertex buffer. The layer refuses the claim rather than
// applying it, and the honest transition is then accepted.
CG_TEST(rhi_validation_errors_catch_a_barrier_with_the_wrong_source_state)
{
    Fixture fixture;
    CG_REQUIRE(fixture.Build());

    CommandList list;
    CommandListDesc desc{};
    desc.queue = QueueKind::Graphics;
    auto created = fixture.device.CreateCommandList(desc);
    CG_REQUIRE(static_cast<bool>(created));
    list = std::move(created.Value());
    CG_REQUIRE(list.Begin().IsOk());

    const BufferBarrier first{fixture.vertexBuffer, ResourceState::Undefined,
                              ResourceState::VertexBuffer};
    CG_REQUIRE(
        list.Barrier(Span<const BufferBarrier>(&first, 1), Span<const TextureBarrier>{}).IsOk());

    const BufferBarrier wrong{fixture.vertexBuffer, ResourceState::CopySource,
                              ResourceState::ShaderRead};
    const auto rejected =
        list.Barrier(Span<const BufferBarrier>(&wrong, 1), Span<const TextureBarrier>{});
    CG_EXPECT_FALSE(rejected);
    if (!rejected)
    {
        CG_EXPECT_EQ(rejected.Error().code, ErrorCode::InvalidArgument);
        // The message describes the state problem, not just "barrier", so a
        // caller can tell it from a transition that was simply forgotten.
        CG_EXPECT_TRUE(std::strstr(rejected.Error().message, "state") != nullptr ||
                       std::strstr(rejected.Error().message, "missing") != nullptr ||
                       std::strstr(rejected.Error().message, "duplicated") != nullptr);
    }

    const BufferBarrier right{fixture.vertexBuffer, ResourceState::VertexBuffer,
                              ResourceState::CopySource};
    CG_EXPECT_TRUE(
        list.Barrier(Span<const BufferBarrier>(&right, 1), Span<const TextureBarrier>{}).IsOk());
}

// The pipeline layout declares the per-material slot for the draw, and nothing
// is bound there. The error names the slot, so the caller knows which group of
// the four to bind.
CG_TEST(rhi_validation_errors_catch_a_draw_with_an_unbound_group)
{
    Fixture fixture;
    CG_REQUIRE(fixture.Build());

    CommandList list;
    CommandListDesc desc{};
    desc.queue = QueueKind::Graphics;
    auto created = fixture.device.CreateCommandList(desc);
    CG_REQUIRE(static_cast<bool>(created));
    list = std::move(created.Value());
    CG_REQUIRE(list.Begin().IsOk());
    CG_REQUIRE(fixture.TransitionTargets(list));
    CG_REQUIRE(fixture.BeginPass(list));
    CG_REQUIRE(list.SetPipeline(fixture.graphicsPipeline).IsOk());

    const BufferBarrier vertex{fixture.vertexBuffer, ResourceState::Undefined,
                               ResourceState::VertexBuffer};
    CG_REQUIRE(
        list.Barrier(Span<const BufferBarrier>(&vertex, 1), Span<const TextureBarrier>{}).IsOk());
    CG_REQUIRE(list.SetVertexBuffer(0, fixture.vertexBuffer).IsOk());

    const auto unbound = list.Draw(DrawArguments{3, 1, 0, 0});
    CG_EXPECT_FALSE(unbound);
    if (!unbound)
    {
        CG_EXPECT_EQ(unbound.Error().code, ErrorCode::InvalidArgument);
        CG_EXPECT_TRUE(std::strstr(unbound.Error().message, "per-material") != nullptr);
    }

    const BufferBarrier uniform{fixture.uniformBuffer, ResourceState::Undefined,
                                ResourceState::ConstantBuffer};
    CG_REQUIRE(
        list.Barrier(Span<const BufferBarrier>(&uniform, 1), Span<const TextureBarrier>{}).IsOk());
    CG_REQUIRE(list.SetBindGroup(BindGroupSlot::PerMaterial, fixture.group).IsOk());
    CG_EXPECT_TRUE(list.Draw(DrawArguments{3, 1, 0, 0}).IsOk());
    CG_EXPECT_EQ(list.Stats().draws, u32{1});
}

// A retained buffer that a submission is still reading cannot be destroyed: the
// destroy is the doorway to reusing its storage, so the guard the submission
// stamped on it is what the wrapper checks, and the buffer stays alive until
// the fence that holds it is reached.
CG_TEST(rhi_validation_errors_catch_a_destroy_while_in_flight)
{
    HeapAllocator heap;
    Device device;
    if (!OpenD3D12Device(device, heap))
    {
        CG_SKIP("this build has no Direct3D 12 adapter to record on");
        return;
    }

    RejectionWatch watch{};
    cyber::core::CallbackSink sink{&WatchRejections, &watch};
    CG_REQUIRE(cyber::core::DefaultLogger().AddSink(&sink));

    auto gate = device.CreateFence(FenceDesc{});
    auto done = device.CreateFence(FenceDesc{});
    CG_REQUIRE(static_cast<bool>(gate) && static_cast<bool>(done));

    BufferDesc sourceDesc{};
    sourceDesc.usage = BufferUsage::CopySource | BufferUsage::HostVisible;
    sourceDesc.sizeBytes = 256;
    sourceDesc.label = "in flight source";
    auto source = device.CreateBuffer(sourceDesc);
    CG_REQUIRE(static_cast<bool>(source));

    BufferDesc destinationDesc{};
    destinationDesc.usage = BufferUsage::Vertex | BufferUsage::CopyDestination;
    destinationDesc.sizeBytes = 256;
    destinationDesc.label = "in flight destination";
    auto destination = device.CreateBuffer(destinationDesc);
    CG_REQUIRE(static_cast<bool>(destination));

    CommandListDesc listDesc{};
    listDesc.queue = QueueKind::Copy;
    auto createdList = device.CreateCommandList(listDesc);
    CG_REQUIRE(static_cast<bool>(createdList));
    CommandList list = std::move(createdList.Value());
    CG_REQUIRE(list.Begin().IsOk());
    const BufferBarrier into[]{
        {source.Value(), ResourceState::Undefined, ResourceState::CopySource},
        {destination.Value(), ResourceState::Undefined, ResourceState::CopyDestination}};
    CG_REQUIRE(
        list.Barrier(Span<const BufferBarrier>(into, 2), Span<const TextureBarrier>{}).IsOk());
    const BufferCopy copy{source.Value(), 0, destination.Value(), 0, 16};
    CG_REQUIRE(list.CopyBuffer(copy).IsOk());
    CG_REQUIRE(list.End().IsOk());

    CG_REQUIRE(SubmitGatedCopy(device, list, gate.Value(), done.Value()));

    // The submission stamped both buffers with a fence the GPU has not reached
    // yet, so the destroy is refused and says so. The naming the phase asks
    // for goes to the `Rhi` channel's warning, which the watch captured.
    const auto refused = device.DestroyBuffer(source.Value());
    CG_EXPECT_FALSE(refused);
    if (!refused)
    {
        CG_EXPECT_EQ(refused.Error().code, ErrorCode::InvalidArgument);
        CG_EXPECT_TRUE(MentionsFlight(refused));
        CG_EXPECT_TRUE(watch.count > 0);
        if (std::strstr(watch.last, "in flight source") == nullptr)
        {
            CG_FAIL(watch.count > 0 ? watch.last : "nothing captured");
        }
    }
    RemoveRejectionWatch(sink);

    // Once the submission has run, the holds are satisfied and the destroy goes
    // through. The refused call did not destroy anything.
    CG_REQUIRE(device.SignalFence(gate.Value(), 1).IsOk());
    CG_REQUIRE(device.WaitFence(done.Value(), 1).IsOk());
    CG_EXPECT_TRUE(device.DestroyBuffer(source.Value()));
    CG_EXPECT_TRUE(device.DestroyBuffer(destination.Value()));
}

// The same refusal protects a transient's memory: the transient may legally be
// destroyed out from under a submission in the backend's allocator, but the
// front end still refuses the destroy whose storage the submission may still
// read, and only uses its place once the fence is reached.
CG_TEST(rhi_validation_errors_catch_a_transient_reuse_before_its_fence)
{
    HeapAllocator heap;
    Device device;
    if (!OpenD3D12Device(device, heap))
    {
        CG_SKIP("this build has no Direct3D 12 adapter to record on");
        return;
    }

    RejectionWatch watch{};
    cyber::core::CallbackSink sink{&WatchRejections, &watch};
    CG_REQUIRE(cyber::core::DefaultLogger().AddSink(&sink));

    auto gate = device.CreateFence(FenceDesc{});
    auto done = device.CreateFence(FenceDesc{});
    CG_REQUIRE(static_cast<bool>(gate) && static_cast<bool>(done));

    BufferDesc sourceDesc{};
    sourceDesc.usage = BufferUsage::CopySource | BufferUsage::HostVisible;
    sourceDesc.sizeBytes = 256;
    sourceDesc.label = "transient staging";
    auto source = device.CreateBuffer(sourceDesc);
    CG_REQUIRE(static_cast<bool>(source));

    BufferDesc transientDesc{};
    transientDesc.usage = BufferUsage::CopyDestination;
    transientDesc.sizeBytes = 64U * 1024U;
    transientDesc.label = "transient target";
    transientDesc.lifetime = MemoryLifetime::Transient;
    auto transient = device.CreateBuffer(transientDesc);
    CG_REQUIRE(static_cast<bool>(transient));

    CommandListDesc listDesc{};
    listDesc.queue = QueueKind::Copy;
    auto createdList = device.CreateCommandList(listDesc);
    CG_REQUIRE(static_cast<bool>(createdList));
    CommandList list = std::move(createdList.Value());
    CG_REQUIRE(list.Begin().IsOk());
    const BufferBarrier into[]{
        {source.Value(), ResourceState::Undefined, ResourceState::CopySource},
        {transient.Value(), ResourceState::Undefined, ResourceState::CopyDestination}};
    CG_REQUIRE(
        list.Barrier(Span<const BufferBarrier>(into, 2), Span<const TextureBarrier>{}).IsOk());
    const BufferCopy copy{source.Value(), 0, transient.Value(), 0, 16};
    CG_REQUIRE(list.CopyBuffer(copy).IsOk());
    CG_REQUIRE(list.End().IsOk());

    CG_REQUIRE(SubmitGatedCopy(device, list, gate.Value(), done.Value()));

    // The gateway to the transient's bytes is a destroy: refuse it while the
    // submission that reads those bytes has not run, and name the resource.
    const auto refused = device.DestroyBuffer(transient.Value());
    CG_EXPECT_FALSE(refused);
    if (!refused)
    {
        CG_EXPECT_EQ(refused.Error().code, ErrorCode::InvalidArgument);
        CG_EXPECT_TRUE(MentionsFlight(refused));
        CG_EXPECT_TRUE(watch.count > 0);
        if (std::strstr(watch.last, "transient target") == nullptr)
        {
            CG_FAIL(watch.count > 0 ? watch.last : "nothing captured");
        }
    }
    RemoveRejectionWatch(sink);

    // The refused call left the transient tracked and its memory intact: the
    // same destroy succeeds once the submission has run.
    CG_REQUIRE(device.SignalFence(gate.Value(), 1).IsOk());
    CG_REQUIRE(device.WaitFence(done.Value(), 1).IsOk());
    CG_EXPECT_TRUE(device.DestroyBuffer(transient.Value()));
    CG_EXPECT_TRUE(device.DestroyBuffer(source.Value()));
}
