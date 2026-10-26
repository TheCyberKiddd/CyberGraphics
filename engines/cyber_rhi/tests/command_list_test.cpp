#include "rhi_fixture.h"

#include <cyber/memory/span.h>
#include <cyber/rhi/command_list.h>
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

bool MakeList(Fixture& fixture, CommandList* out, QueueKind kind = QueueKind::Graphics) noexcept
{
    CommandListDesc desc{};
    desc.queue = kind;
    desc.label = "test list";
    auto created = fixture.device.CreateCommandList(desc);
    if (!created)
    {
        return false;
    }
    *out = std::move(created.Value());
    return true;
}

// Puts the vertex and uniform buffers into the states a draw needs, at the
// device level, so a test about something else does not have to record barriers.
bool ReadyBuffers(Fixture& fixture) noexcept
{
    const BufferBarrier barriers[2] = {
        BufferBarrier{fixture.vertexBuffer, ResourceState::Undefined, ResourceState::VertexBuffer},
        BufferBarrier{fixture.uniformBuffer, ResourceState::Undefined,
                      ResourceState::ConstantBuffer},
    };
    return fixture.device.Transition(Span<const BufferBarrier>(barriers, 2), {}).IsOk();
}

// A list that is recording a pass and has everything a draw needs but the draw
// itself. `Prepare` is false when the caller wants to build the pass by hand.
bool PrepareDraw(Fixture& fixture, CommandList& list) noexcept
{
    if (!ReadyBuffers(fixture))
    {
        return false;
    }
    if (!fixture.TransitionTargets(list))
    {
        return false;
    }
    if (!fixture.BeginPass(list))
    {
        return false;
    }
    if (!list.SetPipeline(fixture.graphicsPipeline))
    {
        return false;
    }
    if (!list.SetVertexBuffer(0, fixture.vertexBuffer))
    {
        return false;
    }
    return list.SetBindGroup(BindGroupSlot::PerMaterial, fixture.group).IsOk();
}

}  // namespace

CG_TEST(rhi_command_list_state_machine)
{
    Fixture fixture;
    CG_REQUIRE(fixture.Build());

    CommandList list;
    CG_REQUIRE(MakeList(fixture, &list));
    CG_EXPECT_TRUE(list.IsValid());
    CG_EXPECT_EQ(list.State(), CommandListState::Initial);
    CG_EXPECT_EQ(list.Queue(), QueueKind::Graphics);
    CG_EXPECT_FALSE(list.IsRecording());

    // A command on a list that is not recording is refused with the reason, not
    // recorded into a list nobody will submit.
    const auto early = list.Draw(DrawArguments{3, 1, 0, 0});
    CG_EXPECT_FALSE(early);
    if (!early)
    {
        CG_EXPECT_EQ(early.Error().code, ErrorCode::InvalidArgument);
    }
    CG_EXPECT_FALSE(list.End());

    CG_EXPECT_TRUE(list.Begin().IsOk());
    CG_EXPECT_EQ(list.State(), CommandListState::Recording);
    CG_EXPECT_TRUE(list.IsRecording());

    // Beginning a list that is already recording is a bug, not a reset.
    CG_EXPECT_FALSE(list.Begin());

    CG_EXPECT_TRUE(list.End().IsOk());
    CG_EXPECT_EQ(list.State(), CommandListState::Ended);
    CG_EXPECT_FALSE(list.End());

    // Re-recording an ended list is legal and starts from nothing.
    CG_EXPECT_TRUE(list.Begin().IsOk());
    CG_EXPECT_EQ(list.State(), CommandListState::Recording);
    CG_EXPECT_EQ(list.Stats().draws, u32{0});

    list.Reset();
    CG_EXPECT_EQ(list.State(), CommandListState::Initial);

    // A default list is empty and rejects every call rather than crashing.
    CommandList empty;
    CG_EXPECT_FALSE(empty.IsValid());
    CG_EXPECT_FALSE(empty.Begin());
    CG_EXPECT_FALSE(empty.Draw(DrawArguments{1, 1, 0, 0}));
    CG_EXPECT_EQ(empty.Stats().draws, u32{0});
}

CG_TEST(rhi_command_list_end_requires_a_closed_pass_and_label)
{
    Fixture fixture;
    CG_REQUIRE(fixture.Build());

    CommandList list;
    CG_REQUIRE(MakeList(fixture, &list));
    CG_REQUIRE(list.Begin().IsOk());
    CG_REQUIRE(fixture.TransitionTargets(list));
    CG_REQUIRE(fixture.BeginPass(list));

    // Ending with a pass open would leave the attachments' store operations
    // unapplied, so it is refused.
    CG_EXPECT_FALSE(list.End());
    CG_EXPECT_TRUE(list.EndRenderPass().IsOk());
    // Ending the pass twice is a bug.
    CG_EXPECT_FALSE(list.EndRenderPass());

    CG_REQUIRE(list.BeginDebugLabel("frame").IsOk());
    CG_EXPECT_FALSE(list.End());
    CG_EXPECT_TRUE(list.EndDebugLabel().IsOk());
    // Closing a label that was never opened is refused.
    CG_EXPECT_FALSE(list.EndDebugLabel());
    CG_EXPECT_TRUE(list.End().IsOk());

    // Reset is legal in any state, including with a pass open, because a caller
    // that abandons a frame must not have to unwind it by hand.
    CG_REQUIRE(list.Begin().IsOk());
    CG_REQUIRE(fixture.TransitionTargets(list));
    CG_REQUIRE(fixture.BeginPass(list));
    list.Reset();
    CG_EXPECT_EQ(list.State(), CommandListState::Initial);
    CG_EXPECT_FALSE(list.CurrentRenderPass().valid);
}

CG_TEST(rhi_command_list_argument_rules)
{
    // A draw with no vertices or no instances does nothing, which is always a
    // caller mistake rather than a valid no-op.
    CG_EXPECT_FALSE(ValidateDrawArguments(DrawArguments{0, 1, 0, 0}));
    CG_EXPECT_FALSE(ValidateDrawArguments(DrawArguments{3, 0, 0, 0}));
    CG_EXPECT_TRUE(ValidateDrawArguments(DrawArguments{3, 1, 0, 0}).IsOk());
    // A vertex range past the 32-bit index space cannot be expressed by a driver.
    const auto overflow = ValidateDrawArguments(DrawArguments{10, 1, 0xFFFFFFFFU, 0});
    CG_EXPECT_FALSE(overflow);
    if (!overflow)
    {
        CG_EXPECT_EQ(overflow.Error().code, ErrorCode::OutOfRange);
    }

    CG_EXPECT_FALSE(ValidateDrawIndexedArguments(DrawIndexedArguments{0, 1, 0, 0, 0}));
    CG_EXPECT_FALSE(ValidateDrawIndexedArguments(DrawIndexedArguments{3, 0, 0, 0, 0}));
    CG_EXPECT_TRUE(ValidateDrawIndexedArguments(DrawIndexedArguments{3, 1, 0, -2, 0}).IsOk());

    CG_EXPECT_FALSE(ValidateDispatchArguments(DispatchArguments{0, 1, 1}));
    CG_EXPECT_FALSE(ValidateDispatchArguments(DispatchArguments{1, 0, 1}));
    CG_EXPECT_TRUE(ValidateDispatchArguments(DispatchArguments{8, 4, 1}).IsOk());
    const auto groups =
        ValidateDispatchArguments(DispatchArguments{kMaxDispatchGroupsPerAxis + 1, 1, 1});
    CG_EXPECT_FALSE(groups);
    if (!groups)
    {
        CG_EXPECT_EQ(groups.Error().code, ErrorCode::OutOfRange);
    }

    // A viewport with a zero or inverted extent draws nothing; a depth range
    // outside 0..1 is not a window range.
    CG_EXPECT_FALSE(ValidateViewport(Viewport{0.0f, 0.0f, 0.0f, 10.0f, 0.0f, 1.0f}));
    CG_EXPECT_FALSE(ValidateViewport(Viewport{0.0f, 0.0f, 10.0f, -1.0f, 0.0f, 1.0f}));
    CG_EXPECT_FALSE(ValidateViewport(Viewport{0.0f, 0.0f, 10.0f, 10.0f, 1.0f, 0.0f}));
    CG_EXPECT_FALSE(ValidateViewport(Viewport{0.0f, 0.0f, 10.0f, 10.0f, 0.0f, 2.0f}));
    CG_EXPECT_TRUE(ValidateViewport(Viewport::Of(64.0f, 64.0f)).IsOk());

    CG_EXPECT_FALSE(ValidateScissor(ScissorRect{0, 0, 0, 10}));
    CG_EXPECT_TRUE(ValidateScissor(ScissorRect{0, 0, 10, 10}).IsOk());

    // An overlapping copy of one buffer is undefined in every API.
    const BufferCopy overlap = BufferCopy{BufferHandle{1, 1}, 0, BufferHandle{1, 1}, 4, 8};
    CG_EXPECT_FALSE(ValidateBufferCopy(overlap));
    const BufferCopy touching = BufferCopy{BufferHandle{1, 1}, 0, BufferHandle{1, 1}, 8, 8};
    CG_EXPECT_TRUE(ValidateBufferCopy(touching).IsOk());
    const BufferCopy zero = BufferCopy{BufferHandle{1, 1}, 0, BufferHandle{2, 1}, 0, 0};
    CG_EXPECT_FALSE(ValidateBufferCopy(zero));

    CG_EXPECT_FALSE(ValidateTextureCopy(TextureCopy{}));
    TextureCopy textureCopy{};
    textureCopy.source = TextureHandle{1, 1};
    textureCopy.destination = TextureHandle{2, 1};
    textureCopy.width = 8;
    CG_EXPECT_TRUE(ValidateTextureCopy(textureCopy).IsOk());

    // A row pitch smaller than one row is a buffer overrun waiting to happen.
    BufferTextureCopy bufferTexture{};
    bufferTexture.texture = TextureHandle{1, 1};
    bufferTexture.width = 8;
    bufferTexture.height = 8;
    bufferTexture.bufferRowPitch = 4;
    CG_EXPECT_FALSE(ValidateBufferTextureCopy(bufferTexture, 32));
    bufferTexture.bufferRowPitch = 32;
    CG_EXPECT_TRUE(ValidateBufferTextureCopy(bufferTexture, 32).IsOk());

    // Indirect arguments need a stride big enough for the structure and an
    // offset a driver can address.
    CG_EXPECT_FALSE(ValidateIndirectArguments(0, 0, 16, 16));
    CG_EXPECT_FALSE(ValidateIndirectArguments(0, 1, 12, 16));
    CG_EXPECT_FALSE(ValidateIndirectArguments(2, 1, 16, 16));
    CG_EXPECT_TRUE(ValidateIndirectArguments(0, 1, 16, 16).IsOk());
}

CG_TEST(rhi_command_list_pipeline_rules)
{
    Fixture fixture;
    CG_REQUIRE(fixture.Build());

    CommandList list;
    CG_REQUIRE(MakeList(fixture, &list));
    CG_REQUIRE(list.Begin().IsOk());

    // A graphics pipeline has nowhere to write outside a pass.
    CG_EXPECT_FALSE(list.SetPipeline(fixture.graphicsPipeline));

    CG_REQUIRE(fixture.TransitionTargets(list));
    CG_REQUIRE(fixture.BeginPass(list));

    // A compute pipeline cannot be bound inside a pass.
    CG_EXPECT_FALSE(list.SetPipeline(fixture.computePipeline));

    // A pipeline whose color attachment format does not match the pass is
    // rejected with the reason, which is the mistake a mixed-format frame makes.
    VertexBinding binding{};
    binding.strideBytes = 12;
    VertexAttribute attribute{};
    attribute.format = Format::R32G32B32Float;
    VertexLayout vertexLayout{};
    vertexLayout.bindings = Span<const VertexBinding>(&binding, 1);
    vertexLayout.attributes = Span<const VertexAttribute>(&attribute, 1);
    ColorAttachmentState srgbAttachment{};
    srgbAttachment.format = Format::RGBA8UnormSrgb;
    GraphicsPipelineDesc srgbDesc = fixture.GraphicsDesc();
    srgbDesc.vertexLayout = vertexLayout;
    srgbDesc.colorAttachments = Span<const ColorAttachmentState>(&srgbAttachment, 1);
    auto srgbPipeline = fixture.device.CreateGraphicsPipeline(srgbDesc);
    CG_REQUIRE(static_cast<bool>(srgbPipeline));
    CG_EXPECT_FALSE(list.SetPipeline(srgbPipeline.Value()));

    // A stale pipeline handle is refused like any other stale handle.
    CG_EXPECT_FALSE(list.SetPipeline(PipelineHandle{}));

    CG_EXPECT_TRUE(list.SetPipeline(fixture.graphicsPipeline).IsOk());
    CG_EXPECT_EQ(list.Stats().pipelineChanges, u32{1});
    CG_EXPECT_TRUE(list.CurrentRenderPass().valid);
    CG_EXPECT_EQ(list.CurrentRenderPass().width, fixture.width);
    CG_EXPECT_EQ(list.CurrentRenderPass().sampleCount, u32{1});
}

CG_TEST(rhi_command_list_binding_rules)
{
    Fixture fixture;
    CG_REQUIRE(fixture.Build());

    CommandList list;
    CG_REQUIRE(MakeList(fixture, &list));
    CG_REQUIRE(list.Begin().IsOk());

    // A group cannot be bound before a pipeline says where it goes.
    CG_EXPECT_FALSE(list.SetBindGroup(BindGroupSlot::PerMaterial, fixture.group));

    CG_REQUIRE(fixture.TransitionTargets(list));
    CG_REQUIRE(fixture.BeginPass(list));
    CG_REQUIRE(list.SetPipeline(fixture.graphicsPipeline).IsOk());

    // The layout uses per-material only, so binding per-draw is a mistake.
    CG_EXPECT_FALSE(list.SetBindGroup(BindGroupSlot::PerDraw, fixture.group));
    // A slot that does not exist is refused too.
    CG_EXPECT_FALSE(list.SetBindGroup(BindGroupSlot::Count, fixture.group));
    CG_EXPECT_TRUE(list.SetBindGroup(BindGroupSlot::PerMaterial, fixture.group).IsOk());
    CG_EXPECT_EQ(list.Stats().bindGroupChanges, u32{1});

    // Unbinding a slot is legal; the next draw reports what is missing.
    CG_EXPECT_TRUE(list.SetBindGroup(BindGroupSlot::PerMaterial, BindGroupHandle{}).IsOk());
}

CG_TEST(rhi_command_list_vertex_and_index_rules)
{
    Fixture fixture;
    CG_REQUIRE(fixture.Build());

    CommandList list;
    CG_REQUIRE(MakeList(fixture, &list));
    CG_REQUIRE(list.Begin().IsOk());
    CG_REQUIRE(fixture.TransitionTargets(list));
    CG_REQUIRE(fixture.BeginPass(list));
    CG_REQUIRE(list.SetPipeline(fixture.graphicsPipeline).IsOk());

    // A vertex buffer needs a buffer with vertex usage: a uniform buffer is not
    // one, however many bytes it has.
    const auto wrongUsage = list.SetVertexBuffer(0, fixture.uniformBuffer);
    CG_EXPECT_FALSE(wrongUsage);
    if (!wrongUsage)
    {
        CG_EXPECT_EQ(wrongUsage.Error().code, ErrorCode::InvalidArgument);
    }
    // The pipeline declares one binding, so binding 4 does not exist.
    CG_EXPECT_FALSE(list.SetVertexBuffer(4, fixture.vertexBuffer));
    // An offset at or past the end reads nothing.
    CG_EXPECT_FALSE(list.SetVertexBuffer(0, fixture.vertexBuffer, 256));

    // The buffer is not in vertex-buffer state yet, so binding it reports the
    // missing barrier rather than accepting it and corrupting a real frame.
    const auto missing = list.SetVertexBuffer(0, fixture.vertexBuffer);
    CG_EXPECT_FALSE(missing);
    if (!missing)
    {
        CG_EXPECT_TRUE(std::strstr(missing.Error().message, "barrier") != nullptr);
    }

    const BufferBarrier barrier{fixture.vertexBuffer, ResourceState::Undefined,
                                ResourceState::VertexBuffer};
    CG_REQUIRE(list.Barrier(Span<const BufferBarrier>(&barrier, 1)).IsOk());
    CG_EXPECT_TRUE(list.SetVertexBuffer(0, fixture.vertexBuffer).IsOk());

    // Index buffers follow the same rules plus an alignment one.
    CG_EXPECT_FALSE(list.SetIndexBuffer(fixture.uniformBuffer));
    CG_EXPECT_FALSE(list.SetIndexBuffer(fixture.indexBuffer, 0, IndexFormat::Count));
    CG_EXPECT_FALSE(list.SetIndexBuffer(fixture.indexBuffer, 2, IndexFormat::Uint32));
    CG_EXPECT_FALSE(list.SetIndexBuffer(fixture.indexBuffer, 0, IndexFormat::Uint32));

    const BufferBarrier indexBarrier{fixture.indexBuffer, ResourceState::Undefined,
                                     ResourceState::IndexBuffer};
    CG_REQUIRE(list.Barrier(Span<const BufferBarrier>(&indexBarrier, 1)).IsOk());
    CG_EXPECT_TRUE(list.SetIndexBuffer(fixture.indexBuffer, 2, IndexFormat::Uint16).IsOk());
}

CG_TEST(rhi_command_list_draw_requires_complete_state)
{
    Fixture fixture;
    CG_REQUIRE(fixture.Build());

    CommandList list;
    CG_REQUIRE(MakeList(fixture, &list));
    CG_REQUIRE(list.Begin().IsOk());
    CG_REQUIRE(fixture.TransitionTargets(list));
    // The pass is opened without a viewport: the draw must say so rather than
    // drawing into a viewport the driver would have to guess.
    CG_REQUIRE(fixture.BeginPass(list));
    CG_REQUIRE(list.SetPipeline(fixture.graphicsPipeline).IsOk());
    CG_REQUIRE(ReadyBuffers(fixture));
    CG_REQUIRE(list.SetVertexBuffer(0, fixture.vertexBuffer).IsOk());
    CG_REQUIRE(list.SetBindGroup(BindGroupSlot::PerMaterial, fixture.group).IsOk());

    CommandList second;
    CG_REQUIRE(MakeList(fixture, &second));
    CG_REQUIRE(second.Begin().IsOk());
    CG_REQUIRE(fixture.TransitionTargets(second));
    CG_REQUIRE(fixture.BeginPass(second));
    CG_REQUIRE(second.SetPipeline(fixture.graphicsPipeline).IsOk());
    CG_REQUIRE(second.SetVertexBuffer(0, fixture.vertexBuffer).IsOk());
    CG_REQUIRE(second.SetBindGroup(BindGroupSlot::PerMaterial, fixture.group).IsOk());

    // A complete list draws and counts it.
    CG_EXPECT_TRUE(second.Draw(DrawArguments{3, 1, 0, 0}).IsOk());
    CG_EXPECT_EQ(second.Stats().draws, u32{1});

    // An indexed draw needs an index buffer.
    const auto indexed = second.DrawIndexed(DrawIndexedArguments{3, 1, 0, 0, 0});
    CG_EXPECT_FALSE(indexed);

    // A viewport outside the pass is refused; one inside it is accepted.
    CG_EXPECT_FALSE(second.SetViewport(Viewport{0.0f, 0.0f, 128.0f, 128.0f, 0.0f, 1.0f}));
    CG_EXPECT_TRUE(second.SetViewport(Viewport{0.0f, 0.0f, 32.0f, 32.0f, 0.0f, 1.0f}).IsOk());
    CG_EXPECT_FALSE(second.SetScissor(ScissorRect{32, 32, 64, 64}));
    CG_EXPECT_TRUE(second.SetScissor(ScissorRect{0, 0, 32, 32}).IsOk());

    // Without a pass there is nothing to draw into.
    CommandList third;
    CG_REQUIRE(MakeList(fixture, &third));
    CG_REQUIRE(third.Begin().IsOk());
    CG_EXPECT_FALSE(third.Draw(DrawArguments{3, 1, 0, 0}));

    // A dispatch inside a pass is refused for the same reason.
    CG_EXPECT_FALSE(second.Dispatch(DispatchArguments{1, 1, 1}));
}

CG_TEST(rhi_command_list_dispatch_rules)
{
    Fixture fixture;
    CG_REQUIRE(fixture.Build());

    CommandList list;
    CG_REQUIRE(MakeList(fixture, &list, QueueKind::Compute));
    CG_REQUIRE(list.Begin().IsOk());

    // No pipeline yet.
    CG_EXPECT_FALSE(list.Dispatch(DispatchArguments{1, 1, 1}));
    CG_EXPECT_TRUE(list.SetPipeline(fixture.computePipeline).IsOk());

    // The compute layout uses the per-material slot, and no group is bound.
    const auto missingGroup = list.Dispatch(DispatchArguments{1, 1, 1});
    CG_EXPECT_FALSE(missingGroup);
    if (!missingGroup)
    {
        CG_EXPECT_TRUE(std::strstr(missingGroup.Error().message, "per-material") != nullptr);
    }

    // The group needs its uniform buffer in constant-buffer state.
    CG_REQUIRE(list.SetBindGroup(BindGroupSlot::PerMaterial, fixture.group).IsOk());
    const auto missingBarrier = list.Dispatch(DispatchArguments{1, 1, 1});
    CG_EXPECT_FALSE(missingBarrier);
    if (!missingBarrier)
    {
        CG_EXPECT_TRUE(std::strstr(missingBarrier.Error().message, "barrier") != nullptr);
    }

    const BufferBarrier barrier{fixture.uniformBuffer, ResourceState::Undefined,
                                ResourceState::ConstantBuffer};
    CG_REQUIRE(list.Barrier(Span<const BufferBarrier>(&barrier, 1)).IsOk());
    CG_EXPECT_TRUE(list.Dispatch(DispatchArguments{8, 8, 1}).IsOk());
    CG_EXPECT_EQ(list.Stats().dispatches, u32{1});
}

CG_TEST(rhi_command_list_copy_and_clear_rules)
{
    Fixture fixture;
    CG_REQUIRE(fixture.Build());

    CommandList list;
    CG_REQUIRE(MakeList(fixture, &list, QueueKind::Copy));
    CG_REQUIRE(list.Begin().IsOk());

    // Both buffers must have the usage the direction needs.
    const BufferCopy wrongUsage{fixture.vertexBuffer, 0, fixture.uniformBuffer, 0, 16};
    const auto rejected = list.CopyBuffer(wrongUsage);
    CG_EXPECT_FALSE(rejected);

    const BufferBarrier source{fixture.vertexBuffer, ResourceState::Undefined,
                               ResourceState::CopySource};
    CG_REQUIRE(list.Barrier(Span<const BufferBarrier>(&source, 1)).IsOk());
    // The destination still lacks copy-destination usage and state.
    CG_EXPECT_FALSE(list.CopyBuffer(wrongUsage));

    // A clear needs a render-target texture in render-target state, which the
    // pass transitions but no copy queue list ever does.
    CG_EXPECT_FALSE(list.ClearColorTexture(fixture.colorTexture, ClearColorValue::Of(0, 0, 0, 1)));
    CG_EXPECT_FALSE(list.ClearColorTexture(TextureHandle{}, ClearColorValue{}));
    // A depth clear needs a depth format, and a color clear refuses one.
    CG_EXPECT_FALSE(list.ClearColorTexture(fixture.depthTexture, ClearColorValue{}));
    CG_EXPECT_FALSE(list.ClearDepthStencilTexture(fixture.colorTexture, ClearDepthStencilValue{}));

    // A draw on a copy queue has no pipeline and no pass; the error is the same
    // one a graphics list gives, which keeps the message honest.
    CG_EXPECT_FALSE(list.Draw(DrawArguments{3, 1, 0, 0}));
}

CG_TEST(rhi_command_list_indirect_rules)
{
    Fixture fixture;
    CG_REQUIRE(fixture.Build());

    CommandList list;
    CG_REQUIRE(MakeList(fixture, &list));
    CG_REQUIRE(list.Begin().IsOk());
    CG_REQUIRE(fixture.TransitionTargets(list));
    CG_REQUIRE(fixture.BeginPass(list));
    CG_REQUIRE(list.SetPipeline(fixture.graphicsPipeline).IsOk());
    CG_REQUIRE(ReadyBuffers(fixture));
    CG_REQUIRE(list.SetVertexBuffer(0, fixture.vertexBuffer).IsOk());
    CG_REQUIRE(list.SetBindGroup(BindGroupSlot::PerMaterial, fixture.group).IsOk());

    // The stride must be big enough for the argument structure.
    CG_EXPECT_FALSE(list.DrawIndirect(fixture.indirectBuffer, 0, 1, 8));
    // The argument buffer is not in indirect-argument state yet.
    const auto missing = list.DrawIndirect(fixture.indirectBuffer, 0, 1, 16);
    CG_EXPECT_FALSE(missing);
    if (!missing)
    {
        CG_EXPECT_TRUE(std::strstr(missing.Error().message, "barrier") != nullptr);
    }

    const BufferBarrier barrier{fixture.indirectBuffer, ResourceState::Undefined,
                                ResourceState::IndirectArgument};
    CG_REQUIRE(list.Barrier(Span<const BufferBarrier>(&barrier, 1)).IsOk());
    CG_EXPECT_TRUE(list.DrawIndirect(fixture.indirectBuffer, 0, 1, 16).IsOk());
    // The range must stay inside the buffer: four 16-byte arguments do not fit
    // in 64 bytes with a 32-byte stride.
    CG_EXPECT_FALSE(list.DrawIndirect(fixture.indirectBuffer, 0, 3, 32));
    CG_EXPECT_FALSE(list.DrawIndexedIndirect(fixture.indirectBuffer, 0, 1, 16));
}

CG_TEST(rhi_command_list_push_constant_rules)
{
    Fixture fixture;
    CG_REQUIRE(fixture.Build());

    // A layout with one range the pixel stage reads.
    const PushConstantRange range{0, 16, ShaderStageBit(ShaderStage::Pixel)};
    PipelineLayoutDesc layoutDesc{};
    layoutDesc.groups[static_cast<u32>(BindGroupSlot::PerMaterial)] = fixture.groupLayout;
    layoutDesc.pushConstants = Span<const PushConstantRange>(&range, 1);
    auto layout = fixture.device.CreatePipelineLayout(layoutDesc);
    CG_REQUIRE(static_cast<bool>(layout));

    VertexBinding binding{};
    binding.strideBytes = 12;
    VertexAttribute attribute{};
    attribute.format = Format::R32G32B32Float;
    VertexLayout vertexLayout{};
    vertexLayout.bindings = Span<const VertexBinding>(&binding, 1);
    vertexLayout.attributes = Span<const VertexAttribute>(&attribute, 1);
    ColorAttachmentState attachment{};
    attachment.format = fixture.colorFormat;
    GraphicsPipelineDesc desc = fixture.GraphicsDesc();
    desc.layout = layout.Value();
    desc.vertexLayout = vertexLayout;
    desc.colorAttachments = Span<const ColorAttachmentState>(&attachment, 1);
    auto pipeline = fixture.device.CreateGraphicsPipeline(desc);
    CG_REQUIRE(static_cast<bool>(pipeline));

    CommandList list;
    CG_REQUIRE(MakeList(fixture, &list));
    CG_REQUIRE(list.Begin().IsOk());
    CG_REQUIRE(fixture.TransitionTargets(list));
    CG_REQUIRE(fixture.BeginPass(list));
    CG_REQUIRE(list.SetPipeline(pipeline.Value()).IsOk());

    const cyber::memory::Byte data[16] = {};
    const auto bytes = Span<const cyber::memory::Byte>(data, 16);
    // The range is visible to the pixel stage only, so a vertex write is outside
    // it, and an offset past the end is outside it too.
    CG_EXPECT_FALSE(list.PushConstants(ShaderStageBit(ShaderStage::Vertex), 0, bytes));
    CG_EXPECT_FALSE(list.PushConstants(ShaderStageBit(ShaderStage::Pixel), 8, bytes));
    CG_EXPECT_FALSE(list.PushConstants(0, 0, bytes));
    CG_EXPECT_TRUE(list.PushConstants(ShaderStageBit(ShaderStage::Pixel), 0, bytes).IsOk());
    // Push constant writes are four byte aligned.
    CG_EXPECT_FALSE(list.PushConstants(ShaderStageBit(ShaderStage::Pixel), 2, bytes));
    CG_EXPECT_EQ(list.Stats().pushConstants, u32{1});
}

CG_TEST(rhi_command_list_stats_report_what_was_recorded)
{
    Fixture fixture;
    CG_REQUIRE(fixture.Build());

    CommandList list;
    CG_REQUIRE(MakeList(fixture, &list));
    CG_REQUIRE(list.Begin().IsOk());
    CG_REQUIRE(PrepareDraw(fixture, list));
    CG_REQUIRE(list.BeginDebugLabel("geometry").IsOk());
    CG_REQUIRE(list.Draw(DrawArguments{3, 1, 0, 0}).IsOk());
    CG_REQUIRE(list.Draw(DrawArguments{6, 1, 3, 0}).IsOk());
    CG_REQUIRE(list.EndDebugLabel().IsOk());
    CG_REQUIRE(list.EndRenderPass().IsOk());
    CG_REQUIRE(list.End().IsOk());

    const CommandListStats stats = list.Stats();
    CG_EXPECT_EQ(stats.renderPasses, u32{1});
    CG_EXPECT_EQ(stats.draws, u32{2});
    CG_EXPECT_EQ(stats.pipelineChanges, u32{1});
    CG_EXPECT_EQ(stats.bindGroupChanges, u32{1});
    CG_EXPECT_EQ(stats.debugLabels, u32{1});
    CG_EXPECT_EQ(stats.barriers, u32{2});

    // A reset forgets the counters with the recording.
    list.Reset();
    CG_EXPECT_EQ(list.Stats().draws, u32{0});
    CG_EXPECT_EQ(list.Stats().renderPasses, u32{0});
}
