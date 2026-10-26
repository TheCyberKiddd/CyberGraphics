#include "rhi_fixture.h"

#include <cyber/rhi/frame_ring.h>
#include <cyber/test/test.h>

#include <utility>

using namespace cyber::rhi;
using namespace cyber::rhi::test;
using cyber::core::ErrorCode;
using cyber::core::u32;
using cyber::core::u64;

CG_TEST(rhi_frame_ring_creation_rules)
{
    Fixture fixture;
    CG_REQUIRE(fixture.Build());

    // No fence, no ring: a ring without a timeline cannot know when a slot is
    // free.
    CG_EXPECT_FALSE(FrameRing::Create(FrameRingDesc{}));
    FrameRingDesc desc{};
    desc.fence = fixture.fence;
    desc.framesInFlight = 0;
    CG_EXPECT_FALSE(FrameRing::Create(desc));

    CG_EXPECT_FALSE(FrameRing::Create(desc));
    desc.framesInFlight = kMaxFramesInFlight + 1;
    CG_EXPECT_FALSE(FrameRing::Create(desc));

    desc.framesInFlight = 3;
    auto created = FrameRing::Create(desc);
    CG_REQUIRE(static_cast<bool>(created));
    FrameRing ring = created.Value();
    CG_EXPECT_TRUE(ring.IsValid());
    CG_EXPECT_EQ(ring.FramesInFlight(), u32{3});
    CG_EXPECT_EQ(ring.FrameIndex(), u64{0});
    CG_EXPECT_EQ(ring.NextSlot(), u32{0});
    CG_EXPECT_EQ(ring.Fence(), fixture.fence);

    FrameRing empty;
    CG_EXPECT_FALSE(empty.IsValid());
    CG_EXPECT_FALSE(empty.BeginFrame(fixture.device));
}

CG_TEST(rhi_frame_ring_cycles_slots_and_values)
{
    Fixture fixture;
    CG_REQUIRE(fixture.Build());

    FrameRingDesc desc{};
    desc.fence = fixture.fence;
    desc.framesInFlight = 3;
    auto created = FrameRing::Create(desc);
    CG_REQUIRE(static_cast<bool>(created));
    FrameRing ring = created.Value();

    // The first three frames never wait: no slot has been used yet.
    const u32 expectedSlots[3] = {0, 1, 2};
    for (u64 frame = 0; frame < 3; ++frame)
    {
        auto slot = ring.BeginFrame(fixture.device, 0);
        CG_REQUIRE(static_cast<bool>(slot));
        CG_EXPECT_TRUE(slot.Value().valid);
        CG_EXPECT_EQ(slot.Value().slot, expectedSlots[frame]);
        CG_EXPECT_EQ(slot.Value().frameIndex, frame);
        CG_EXPECT_EQ(slot.Value().signalValue, frame + 1);
        CG_EXPECT_EQ(ring.SignalValueForFrame(frame), frame + 1);
    }
    // A frame the ring has not handed out has no value to wait on.
    CG_EXPECT_EQ(ring.SignalValueForFrame(3), u64{0});

    // Frame 3 reuses slot 0, so it waits for frame 0's value.
    const auto blocked = ring.BeginFrame(fixture.device, 0);
    CG_EXPECT_FALSE(blocked);
    if (!blocked)
    {
        CG_EXPECT_EQ(blocked.Error().code, ErrorCode::NotFound);
    }
    CG_EXPECT_EQ(ring.FrameIndex(), u64{3});

    // Reaching frame 0's value frees the slot.
    CG_REQUIRE(fixture.device.SignalFence(fixture.fence, 1).IsOk());
    auto reused = ring.BeginFrame(fixture.device, 0);
    CG_REQUIRE(static_cast<bool>(reused));
    CG_EXPECT_EQ(reused.Value().slot, u32{0});
    CG_EXPECT_EQ(reused.Value().frameIndex, u64{3});
    CG_EXPECT_EQ(reused.Value().signalValue, u64{4});
    CG_EXPECT_EQ(ring.NextSlot(), u32{1});

    // Frame 4 waits for frame 1, not for frame 0 again and not for frame 3: a
    // ring of three must let three frames be in flight.
    CG_EXPECT_FALSE(ring.BeginFrame(fixture.device, 0));
    CG_REQUIRE(fixture.device.SignalFence(fixture.fence, 2).IsOk());
    auto next = ring.BeginFrame(fixture.device, 0);
    CG_REQUIRE(static_cast<bool>(next));
    CG_EXPECT_EQ(next.Value().slot, u32{1});
    // The frame that just started has not completed.
    CG_EXPECT_FALSE(ring.IsFrameComplete(fixture.device, next.Value().frameIndex));
    CG_EXPECT_TRUE(ring.IsFrameComplete(fixture.device, 0));
}

CG_TEST(rhi_frame_ring_tracks_completion)
{
    Fixture fixture;
    CG_REQUIRE(fixture.Build());

    FrameRingDesc desc{};
    desc.fence = fixture.fence;
    desc.framesInFlight = 2;
    auto created = FrameRing::Create(desc);
    CG_REQUIRE(static_cast<bool>(created));
    FrameRing ring = created.Value();

    auto first = ring.BeginFrame(fixture.device);
    CG_REQUIRE(static_cast<bool>(first));
    // A frame that has not been signalled is not complete, even though the ring
    // handed it out.
    CG_EXPECT_FALSE(ring.IsFrameComplete(fixture.device, 0));

    CG_REQUIRE(fixture.device.SignalFence(fixture.fence, first.Value().signalValue).IsOk());
    CG_EXPECT_TRUE(ring.IsFrameComplete(fixture.device, 0));
    // A frame the ring has not handed out is never complete.
    CG_EXPECT_FALSE(ring.IsFrameComplete(fixture.device, 5));

    // With two frames in flight the third frame must wait for the first.
    auto second = ring.BeginFrame(fixture.device);
    CG_REQUIRE(static_cast<bool>(second));
    CG_EXPECT_EQ(second.Value().slot, u32{1});
    CG_EXPECT_TRUE(static_cast<bool>(ring.BeginFrame(fixture.device, 0)));

    // Reset lets a caller restart after the device has gone idle.
    ring.Reset();
    CG_EXPECT_EQ(ring.FrameIndex(), u64{0});
    CG_EXPECT_EQ(ring.NextSlot(), u32{0});
}

CG_TEST(rhi_frame_ring_requires_a_live_device)
{
    // A ring over an empty device cannot wait on anything, and saying so is
    // better than reading through a null interface.
    FrameRingDesc desc{};
    FenceHandle fence;
    fence.index = 1;
    fence.generation = 1;
    desc.fence = fence;
    desc.framesInFlight = 2;
    auto created = FrameRing::Create(desc);
    CG_REQUIRE(static_cast<bool>(created));
    FrameRing ring = created.Value();

    Device empty;
    CG_EXPECT_FALSE(ring.BeginFrame(empty, 0));
    CG_EXPECT_FALSE(ring.IsFrameComplete(empty, 0));
}
