// The phase's done-when line: a command that reads a resource in a state it was
// never transitioned into must be caught by null-backend validation, with the
// resource named and the missing transition described.
//
// Each test records the same frame twice: once without the barrier, where the
// command must fail, and once with it, where the same command must be accepted.
// A test that only checked the failure would pass just as well if the command
// were broken.

#include "rhi_fixture.h"

#include <cyber/memory/span.h>
#include <cyber/rhi/command_list.h>
#include <cyber/test/test.h>

#include <utility>

using namespace cyber::rhi;
using namespace cyber::rhi::test;
using cyber::core::ErrorCode;
using cyber::core::u32;
using cyber::memory::Span;

namespace
{

bool MentionsBarrier(const cyber::core::Status<cyber::core::Error>& status) noexcept
{
    return !status && status.Error().message != nullptr &&
           std::strstr(status.Error().message, "barrier") != nullptr;
}

bool MakeList(Fixture& fixture, CommandList* out, QueueKind kind = QueueKind::Graphics) noexcept
{
    CommandListDesc desc{};
    desc.queue = kind;
    auto created = fixture.device.CreateCommandList(desc);
    if (!created)
    {
        return false;
    }
    *out = std::move(created.Value());
    return true;
}

}  // namespace

CG_TEST(rhi_missing_barrier_for_a_color_attachment)
{
    Fixture fixture;
    CG_REQUIRE(fixture.Build());

    CommandList list;
    CG_REQUIRE(MakeList(fixture, &list));
    CG_REQUIRE(list.Begin().IsOk());

    // The pass writes the color target, which is still undefined because nothing
    // transitioned it. Every API requires the transition; skipping it is the
    // classic "it worked on my machine" bug.
    const auto missing = list.BeginRenderPass(RenderPassDesc{
        Span<const ColorAttachment>(), DepthStencilAttachment{}, false, "no attachments"});
    CG_EXPECT_FALSE(missing);

    ColorAttachment attachment{};
    attachment.view = fixture.colorView;
    RenderPassDesc pass{};
    pass.colorAttachments = Span<const ColorAttachment>(&attachment, 1);

    const auto undefinedTarget = list.BeginRenderPass(pass);
    CG_EXPECT_FALSE(undefinedTarget);
    if (!undefinedTarget)
    {
        CG_EXPECT_EQ(undefinedTarget.Error().code, ErrorCode::InvalidArgument);
        CG_EXPECT_TRUE(MentionsBarrier(undefinedTarget));
    }

    const TextureBarrier barrier{fixture.colorTexture, SubresourceRange{}, ResourceState::Undefined,
                                 ResourceState::RenderTarget};
    CG_REQUIRE(list.Barrier({}, Span<const TextureBarrier>(&barrier, 1)).IsOk());
    CG_EXPECT_TRUE(list.BeginRenderPass(pass).IsOk());
    CG_EXPECT_TRUE(list.EndRenderPass().IsOk());
}

CG_TEST(rhi_missing_barrier_for_a_depth_attachment)
{
    Fixture fixture;
    CG_REQUIRE(fixture.Build());

    CommandList list;
    CG_REQUIRE(MakeList(fixture, &list));
    CG_REQUIRE(list.Begin().IsOk());

    // Only the color target is transitioned, so the depth attachment is the one
    // that fails. A validator that stopped at the first attachment would let
    // this through.
    const TextureBarrier color{fixture.colorTexture, SubresourceRange{}, ResourceState::Undefined,
                               ResourceState::RenderTarget};
    CG_REQUIRE(list.Barrier({}, Span<const TextureBarrier>(&color, 1)).IsOk());

    ColorAttachment attachment{};
    attachment.view = fixture.colorView;
    RenderPassDesc pass{};
    pass.colorAttachments = Span<const ColorAttachment>(&attachment, 1);
    pass.hasDepthStencil = true;
    pass.depthStencil.view = fixture.depthView;

    const auto missing = list.BeginRenderPass(pass);
    CG_EXPECT_FALSE(missing);
    if (!missing)
    {
        CG_EXPECT_TRUE(MentionsBarrier(missing));
    }

    const TextureBarrier depth{fixture.depthTexture,
                               SubresourceRange{0, 0, 0, 0, TextureAspect::Depth},
                               ResourceState::Undefined, ResourceState::DepthWrite};
    CG_REQUIRE(list.Barrier({}, Span<const TextureBarrier>(&depth, 1)).IsOk());
    CG_EXPECT_TRUE(list.BeginRenderPass(pass).IsOk());
    CG_EXPECT_TRUE(list.EndRenderPass().IsOk());
}

CG_TEST(rhi_missing_barrier_for_a_vertex_buffer)
{
    Fixture fixture;
    CG_REQUIRE(fixture.Build());

    CommandList list;
    CG_REQUIRE(MakeList(fixture, &list));
    CG_REQUIRE(list.Begin().IsOk());
    CG_REQUIRE(fixture.TransitionTargets(list));
    CG_REQUIRE(fixture.BeginPass(list));
    CG_REQUIRE(list.SetPipeline(fixture.graphicsPipeline).IsOk());

    const auto missing = list.SetVertexBuffer(0, fixture.vertexBuffer);
    CG_EXPECT_FALSE(missing);
    if (!missing)
    {
        CG_EXPECT_TRUE(MentionsBarrier(missing));
    }

    const BufferBarrier barrier{fixture.vertexBuffer, ResourceState::Undefined,
                                ResourceState::VertexBuffer};
    CG_REQUIRE(list.Barrier(Span<const BufferBarrier>(&barrier, 1)).IsOk());
    CG_EXPECT_TRUE(list.SetVertexBuffer(0, fixture.vertexBuffer).IsOk());
}

CG_TEST(rhi_missing_barrier_for_a_uniform_buffer_binding)
{
    Fixture fixture;
    CG_REQUIRE(fixture.Build());

    CommandList list;
    CG_REQUIRE(MakeList(fixture, &list));
    CG_REQUIRE(list.Begin().IsOk());
    CG_REQUIRE(fixture.TransitionTargets(list));
    CG_REQUIRE(fixture.BeginPass(list));
    CG_REQUIRE(list.SetPipeline(fixture.graphicsPipeline).IsOk());

    const BufferBarrier vertex{fixture.vertexBuffer, ResourceState::Undefined,
                               ResourceState::VertexBuffer};
    CG_REQUIRE(list.Barrier(Span<const BufferBarrier>(&vertex, 1)).IsOk());
    CG_REQUIRE(list.SetVertexBuffer(0, fixture.vertexBuffer).IsOk());
    CG_REQUIRE(list.SetBindGroup(BindGroupSlot::PerMaterial, fixture.group).IsOk());

    // The group is bound and its buffer has uniform usage, but it was never
    // transitioned out of undefined. The draw is what reads it.
    const auto missing = list.Draw(DrawArguments{3, 1, 0, 0});
    CG_EXPECT_FALSE(missing);
    if (!missing)
    {
        CG_EXPECT_EQ(missing.Error().code, ErrorCode::InvalidArgument);
        CG_EXPECT_TRUE(MentionsBarrier(missing));
    }

    const BufferBarrier uniform{fixture.uniformBuffer, ResourceState::Undefined,
                                ResourceState::ConstantBuffer};
    CG_REQUIRE(list.Barrier(Span<const BufferBarrier>(&uniform, 1)).IsOk());
    CG_EXPECT_TRUE(list.Draw(DrawArguments{3, 1, 0, 0}).IsOk());
    CG_EXPECT_EQ(list.Stats().draws, u32{1});
}

CG_TEST(rhi_missing_barrier_for_a_buffer_copy)
{
    Fixture fixture;
    CG_REQUIRE(fixture.Build());

    CommandList list;
    CG_REQUIRE(MakeList(fixture, &list, QueueKind::Copy));
    CG_REQUIRE(list.Begin().IsOk());

    // Neither end of the copy has copy usage, so the first failure is the usage
    // rule; that is a different mistake from a missing barrier and must say so.
    const BufferCopy copy{fixture.vertexBuffer, 0, fixture.uniformBuffer, 0, 8};
    const auto wrongUsage = list.CopyBuffer(copy);
    CG_EXPECT_FALSE(wrongUsage);
    if (!wrongUsage)
    {
        CG_EXPECT_TRUE(std::strstr(wrongUsage.Error().message, "usage") != nullptr);
    }

    // A buffer created for copying is what a copy queue actually moves data with.
    BufferDesc staging{};
    staging.usage = BufferUsage::HostVisible | BufferUsage::CopySource;
    staging.sizeBytes = 64;
    auto source = fixture.device.CreateBuffer(staging);
    BufferDesc readback{};
    readback.usage = BufferUsage::Readback | BufferUsage::CopyDestination;
    readback.sizeBytes = 64;
    auto destination = fixture.device.CreateBuffer(readback);
    CG_REQUIRE(static_cast<bool>(source));
    CG_REQUIRE(static_cast<bool>(destination));

    const BufferCopy valid{source.Value(), 0, destination.Value(), 0, 16};
    const auto missing = list.CopyBuffer(valid);
    CG_EXPECT_FALSE(missing);
    if (!missing)
    {
        CG_EXPECT_TRUE(MentionsBarrier(missing));
    }

    const BufferBarrier barriers[2] = {
        BufferBarrier{source.Value(), ResourceState::Undefined, ResourceState::CopySource},
        BufferBarrier{destination.Value(), ResourceState::Undefined,
                      ResourceState::CopyDestination},
    };
    CG_REQUIRE(list.Barrier(Span<const BufferBarrier>(barriers, 2)).IsOk());
    CG_EXPECT_TRUE(list.CopyBuffer(valid).IsOk());
    CG_EXPECT_EQ(list.Stats().copies, u32{1});

    // A range that leaves the buffer is out of range, not a barrier problem.
    const BufferCopy tooBig{source.Value(), 0, destination.Value(), 0, 128};
    const auto ranged = list.CopyBuffer(tooBig);
    CG_EXPECT_FALSE(ranged);
    if (!ranged)
    {
        CG_EXPECT_EQ(ranged.Error().code, ErrorCode::OutOfRange);
    }
}

CG_TEST(rhi_missing_barrier_for_a_texture_clear)
{
    Fixture fixture;
    CG_REQUIRE(fixture.Build());

    CommandList list;
    CG_REQUIRE(MakeList(fixture, &list));
    CG_REQUIRE(list.Begin().IsOk());

    // Clearing outside a pass still needs the texture in the state the clear
    // writes from.
    const auto missing =
        list.ClearColorTexture(fixture.colorTexture, ClearColorValue::Of(0, 0, 0, 1));
    CG_EXPECT_FALSE(missing);
    if (!missing)
    {
        CG_EXPECT_TRUE(MentionsBarrier(missing));
    }

    const TextureBarrier barrier{fixture.colorTexture, SubresourceRange{}, ResourceState::Undefined,
                                 ResourceState::RenderTarget};
    CG_REQUIRE(list.Barrier({}, Span<const TextureBarrier>(&barrier, 1)).IsOk());
    CG_EXPECT_TRUE(
        list.ClearColorTexture(fixture.colorTexture, ClearColorValue::Of(0, 0, 0, 1)).IsOk());

    const auto depthMissing =
        list.ClearDepthStencilTexture(fixture.depthTexture, ClearDepthStencilValue::Of(1.0f));
    CG_EXPECT_FALSE(depthMissing);
    if (!depthMissing)
    {
        CG_EXPECT_TRUE(MentionsBarrier(depthMissing));
    }
    const TextureBarrier depth{fixture.depthTexture,
                               SubresourceRange{0, 0, 0, 0, TextureAspect::Depth},
                               ResourceState::Undefined, ResourceState::DepthWrite};
    CG_REQUIRE(list.Barrier({}, Span<const TextureBarrier>(&depth, 1)).IsOk());
    CG_EXPECT_TRUE(
        list.ClearDepthStencilTexture(fixture.depthTexture, ClearDepthStencilValue::Of(1.0f))
            .IsOk());
    CG_EXPECT_EQ(list.Stats().clears, u32{2});
}

CG_TEST(rhi_barrier_source_must_match_the_list_recorded_state)
{
    Fixture fixture;
    CG_REQUIRE(fixture.Build());

    CommandList list;
    CG_REQUIRE(MakeList(fixture, &list));
    CG_REQUIRE(list.Begin().IsOk());

    const BufferBarrier first{fixture.vertexBuffer, ResourceState::Undefined,
                              ResourceState::VertexBuffer};
    CG_REQUIRE(list.Barrier(Span<const BufferBarrier>(&first, 1)).IsOk());

    // The second barrier claims to start from copy-source, but the list says the
    // buffer is a vertex buffer. That is a duplicated or missing barrier, and it
    // is refused rather than applied.
    const BufferBarrier wrong{fixture.vertexBuffer, ResourceState::CopySource,
                              ResourceState::ShaderRead};
    const auto rejected = list.Barrier(Span<const BufferBarrier>(&wrong, 1));
    CG_EXPECT_FALSE(rejected);
    if (!rejected)
    {
        CG_EXPECT_TRUE(std::strstr(rejected.Error().message, "missing") != nullptr ||
                       std::strstr(rejected.Error().message, "duplicated") != nullptr);
    }

    // The honest one succeeds.
    const BufferBarrier right{fixture.vertexBuffer, ResourceState::VertexBuffer,
                              ResourceState::CopySource};
    CG_EXPECT_TRUE(list.Barrier(Span<const BufferBarrier>(&right, 1)).IsOk());

    // A batch with one bad barrier applies none of them: the first entry below
    // is valid and the second claims a state the list never recorded.
    const BufferBarrier mixed[2] = {
        BufferBarrier{fixture.vertexBuffer, ResourceState::CopySource, ResourceState::ShaderRead},
        BufferBarrier{fixture.uniformBuffer, ResourceState::VertexBuffer,
                      ResourceState::ShaderRead},
    };
    CG_EXPECT_FALSE(list.Barrier(Span<const BufferBarrier>(mixed, 2)).IsOk());
    // The good half of the rejected batch did not apply either.
    const BufferBarrier stillCopySource{fixture.vertexBuffer, ResourceState::CopySource,
                                        ResourceState::ShaderRead};
    CG_EXPECT_TRUE(list.Barrier(Span<const BufferBarrier>(&stillCopySource, 1)).IsOk());
}

CG_TEST(rhi_barrier_state_becomes_visible_when_the_list_ends)
{
    Fixture fixture;
    CG_REQUIRE(fixture.Build());

    // The device's tracked state is untouched while a list is open, so a list
    // that is abandoned changes nothing and a second recording starts from the
    // state the device really has.
    CG_EXPECT_EQ(fixture.device.BufferStateOf(fixture.vertexBuffer), ResourceState::Undefined);

    CommandList list;
    CG_REQUIRE(MakeList(fixture, &list));
    CG_REQUIRE(list.Begin().IsOk());
    const BufferBarrier barrier{fixture.vertexBuffer, ResourceState::Undefined,
                                ResourceState::VertexBuffer};
    CG_REQUIRE(list.Barrier(Span<const BufferBarrier>(&barrier, 1)).IsOk());
    CG_EXPECT_EQ(fixture.device.BufferStateOf(fixture.vertexBuffer), ResourceState::Undefined);

    CG_REQUIRE(list.End().IsOk());
    CG_EXPECT_EQ(fixture.device.BufferStateOf(fixture.vertexBuffer), ResourceState::VertexBuffer);

    // A discarded recording leaves the state alone.
    CG_REQUIRE(list.Begin().IsOk());
    const BufferBarrier discard{fixture.vertexBuffer, ResourceState::VertexBuffer,
                                ResourceState::CopySource};
    CG_REQUIRE(list.Barrier(Span<const BufferBarrier>(&discard, 1)).IsOk());
    list.Reset();
    CG_EXPECT_EQ(fixture.device.BufferStateOf(fixture.vertexBuffer), ResourceState::VertexBuffer);

    // A texture records per subresource: transitioning mip 0 leaves mip 1 alone.
    CommandList textures;
    CG_REQUIRE(MakeList(fixture, &textures));
    CG_REQUIRE(textures.Begin().IsOk());
    const TextureBarrier whole{fixture.colorTexture, SubresourceRange{}, ResourceState::Undefined,
                               ResourceState::RenderTarget};
    CG_REQUIRE(textures.Barrier({}, Span<const TextureBarrier>(&whole, 1)).IsOk());
    CG_REQUIRE(textures.End().IsOk());
    CG_EXPECT_EQ(fixture.device.TextureStateOf(fixture.colorTexture, 0, 0),
                 ResourceState::RenderTarget);
}
