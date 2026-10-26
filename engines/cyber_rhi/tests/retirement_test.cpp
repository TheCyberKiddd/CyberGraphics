#include "rhi_fixture.h"

#include <cyber/rhi/retirement.h>
#include <cyber/test/test.h>

using namespace cyber::rhi;
using namespace cyber::rhi::test;
using cyber::core::ErrorCode;
using cyber::core::u32;
using cyber::core::u64;
using cyber::core::usize;

CG_TEST(rhi_retirement_waits_for_the_fence)
{
    Fixture fixture;
    CG_REQUIRE(fixture.Build());

    BufferDesc desc{};
    desc.usage = BufferUsage::Vertex;
    desc.sizeBytes = 64;
    auto created = fixture.device.CreateBuffer(desc);
    CG_REQUIRE(static_cast<bool>(created));
    const BufferHandle buffer = created.Value();

    RetirementQueue queue(fixture.heap);
    CG_REQUIRE(queue.Retire(buffer, fixture.fence, 2).IsOk());
    CG_EXPECT_EQ(queue.Pending(), usize{1});
    CG_EXPECT_FALSE(queue.Empty());

    // The fence has not reached the value, so nothing is destroyed and the
    // buffer is still usable.
    auto collected = queue.Collect(fixture.device);
    CG_REQUIRE(static_cast<bool>(collected));
    CG_EXPECT_EQ(collected.Value(), u32{0});
    CG_EXPECT_TRUE(fixture.device.BufferInfoOf(buffer).valid);

    CG_REQUIRE(fixture.device.SignalFence(fixture.fence, 2).IsOk());
    collected = queue.Collect(fixture.device);
    CG_REQUIRE(static_cast<bool>(collected));
    CG_EXPECT_EQ(collected.Value(), u32{1});
    CG_EXPECT_FALSE(fixture.device.BufferInfoOf(buffer).valid);
    CG_EXPECT_TRUE(queue.Empty());
}

CG_TEST(rhi_retirement_handles_every_object_kind)
{
    Fixture fixture;
    CG_REQUIRE(fixture.Build());

    TextureHandle sampled;
    TextureViewHandle sampledView;
    CG_REQUIRE(fixture.MakeSampledTexture(&sampled, &sampledView));
    auto sampler = fixture.device.CreateSampler(SamplerDesc{});
    CG_REQUIRE(static_cast<bool>(sampler));
    auto cache = fixture.device.CreatePipelineCache(PipelineCacheDesc{});
    CG_REQUIRE(static_cast<bool>(cache));

    RetirementQueue queue(fixture.heap);
    CG_REQUIRE(queue.Retire(sampled, fixture.fence, 1).IsOk());
    CG_REQUIRE(queue.Retire(sampledView, fixture.fence, 1).IsOk());
    CG_REQUIRE(queue.Retire(sampler.Value(), fixture.fence, 1).IsOk());
    CG_REQUIRE(queue.Retire(cache.Value(), fixture.fence, 1).IsOk());
    CG_REQUIRE(queue.Retire(fixture.group, fixture.fence, 1).IsOk());
    CG_REQUIRE(queue.Retire(fixture.groupLayout, fixture.fence, 1).IsOk());
    CG_REQUIRE(queue.Retire(fixture.pipelineLayout, fixture.fence, 1).IsOk());
    CG_REQUIRE(queue.Retire(fixture.graphicsPipeline, fixture.fence, 1).IsOk());
    CG_REQUIRE(queue.Retire(fixture.computePipeline, fixture.fence, 1).IsOk());
    CG_REQUIRE(queue.Retire(fixture.vertexShader, fixture.fence, 1).IsOk());
    CG_EXPECT_EQ(queue.Pending(), usize{10});

    // Nothing is released before the fence.
    auto collected = queue.Collect(fixture.device);
    CG_REQUIRE(static_cast<bool>(collected));
    CG_EXPECT_EQ(collected.Value(), u32{0});

    CG_REQUIRE(fixture.device.SignalFence(fixture.fence, 1).IsOk());
    collected = queue.Collect(fixture.device);
    CG_REQUIRE(static_cast<bool>(collected));
    // Nine of the ten are counted: destroying the texture also retires its view,
    // so the view entry that follows is a stale handle and is dropped rather
    // than reported. A redundant destroy must not fail a frame.
    CG_EXPECT_EQ(collected.Value(), u32{9});
    CG_EXPECT_TRUE(queue.Empty());
    CG_EXPECT_FALSE(fixture.device.TextureViewInfoOf(sampledView).valid);

    // The pipeline was destroyed by the call that matches its kind, so the
    // handle no longer resolves.
    CG_EXPECT_FALSE(fixture.device.PipelineInfoOf(fixture.graphicsPipeline).valid);
    CG_EXPECT_FALSE(fixture.device.TextureInfoOf(sampled).valid);
}

CG_TEST(rhi_retirement_rejects_invalid_entries)
{
    Fixture fixture;
    CG_REQUIRE(fixture.Build());

    RetirementQueue queue(fixture.heap);
    CG_EXPECT_FALSE(queue.Retire(BufferHandle{}, fixture.fence, 1));
    CG_EXPECT_FALSE(queue.Retire(fixture.vertexBuffer, FenceHandle{}, 1));
    CG_EXPECT_EQ(queue.Pending(), usize{0});

    // A device that is gone cannot collect; the entries must not be silently
    // dropped, because the caller may still want to flush them somewhere.
    CG_REQUIRE(queue.Retire(fixture.vertexBuffer, fixture.fence, 1).IsOk());
    Device empty;
    CG_EXPECT_FALSE(queue.Collect(empty));
    CG_EXPECT_EQ(queue.Pending(), usize{1});
    CG_EXPECT_EQ(queue.Flush(empty), u32{0});
    CG_EXPECT_EQ(queue.Pending(), usize{1});

    // Clearing forgets the entries without touching a device.
    queue.Clear();
    CG_EXPECT_TRUE(queue.Empty());
    CG_EXPECT_EQ(queue.Pending(), usize{0});
}

CG_TEST(rhi_retirement_flush_ignores_fences)
{
    Fixture fixture;
    CG_REQUIRE(fixture.Build());

    BufferDesc desc{};
    desc.usage = BufferUsage::Vertex;
    desc.sizeBytes = 32;
    auto first = fixture.device.CreateBuffer(desc);
    auto second = fixture.device.CreateBuffer(desc);
    CG_REQUIRE(static_cast<bool>(first));
    CG_REQUIRE(static_cast<bool>(second));

    RetirementQueue queue(fixture.heap);
    CG_REQUIRE(queue.Retire(first.Value(), fixture.fence, 100).IsOk());
    CG_REQUIRE(queue.Retire(second.Value(), fixture.fence, 200).IsOk());
    CG_EXPECT_EQ(queue.Pending(), usize{2});

    // A flush is the shutdown path: the caller promised the device is idle.
    CG_EXPECT_EQ(queue.Flush(fixture.device), u32{2});
    CG_EXPECT_TRUE(queue.Empty());
    CG_EXPECT_FALSE(fixture.device.BufferInfoOf(first.Value()).valid);
    CG_EXPECT_FALSE(fixture.device.BufferInfoOf(second.Value()).valid);
}

CG_TEST(rhi_retirement_survives_a_double_destroy)
{
    Fixture fixture;
    CG_REQUIRE(fixture.Build());

    BufferDesc desc{};
    desc.usage = BufferUsage::Vertex;
    desc.sizeBytes = 32;
    auto created = fixture.device.CreateBuffer(desc);
    CG_REQUIRE(static_cast<bool>(created));
    const BufferHandle buffer = created.Value();

    RetirementQueue queue(fixture.heap);
    CG_REQUIRE(queue.Retire(buffer, fixture.fence, 1).IsOk());
    CG_REQUIRE(fixture.device.DestroyBuffer(buffer).IsOk());
    CG_REQUIRE(fixture.device.SignalFence(fixture.fence, 1).IsOk());

    // Destroying something a caller already destroyed is not a failure of the
    // retirement path: the frame still has to finish.
    auto collected = queue.Collect(fixture.device);
    CG_REQUIRE(static_cast<bool>(collected));
    CG_EXPECT_EQ(collected.Value(), u32{0});
    CG_EXPECT_TRUE(queue.Empty());
}
