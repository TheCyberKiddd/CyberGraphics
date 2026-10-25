#include <cyber/rhi/capabilities.h>
#include <cyber/rhi/conventions.h>
#include <cyber/rhi/descriptors.h>
#include <cyber/test/test.h>

using namespace cyber::rhi;
using cyber::core::u32;

CG_TEST(rhi_conventions_boundary_constants)
{
    // The boundary is the engine's convention, not any API's. These are the
    // values the software renderer already uses, so a scene renders the same way
    // on both paths.
    CG_EXPECT_TRUE(kClipSpaceYUp);
    CG_EXPECT_TRUE(kReverseZ);
    CG_EXPECT_TRUE(kTextureOriginTopLeft);
    CG_EXPECT_TRUE(kRightHandedView);
    CG_EXPECT_NEAR(kNdcDepthMin, 0.0f, 0.0f);
    CG_EXPECT_NEAR(kNdcDepthMax, 1.0f, 0.0f);
}

CG_TEST(rhi_conventions_backend_table)
{
    // One row per backend, so a backend added later cannot quietly disagree with
    // the boundary without this test failing.
    struct Row
    {
        BackendKind kind;
        bool yFlip;
        bool zeroToOneDepth;
        bool topLeftOrigin;
    };
    const Row rows[] = {
        {BackendKind::Null, false, true, true},   {BackendKind::D3D12, true, true, true},
        {BackendKind::Vulkan, true, true, true},  {BackendKind::Metal, false, false, false},
        {BackendKind::WebGpu, false, true, true},
    };

    u32 checked = 0;
    for (const Row& row : rows)
    {
        CG_EXPECT_EQ(BackendNeedsClipSpaceYFlip(row.kind), row.yFlip);
        CG_EXPECT_EQ(BackendUsesZeroToOneDepth(row.kind), row.zeroToOneDepth);
        CG_EXPECT_EQ(BackendUsesTopLeftTextureOrigin(row.kind), row.topLeftOrigin);
        ++checked;
    }
    // Every real kind has a row: the count sentinel's value is exactly the number
    // of enumerators before it.
    CG_EXPECT_EQ(checked, kBackendKindCount);
}

CG_TEST(rhi_conventions_out_of_range_kind)
{
    // An unknown kind answers the conservative way instead of reading past the
    // switch: no flip, 0..1 depth, top-left origin.
    const BackendKind unknown = static_cast<BackendKind>(kBackendKindCount + 3);
    CG_EXPECT_FALSE(BackendNeedsClipSpaceYFlip(unknown));
    CG_EXPECT_TRUE(BackendUsesZeroToOneDepth(unknown));
    CG_EXPECT_TRUE(BackendUsesTopLeftTextureOrigin(unknown));
    CG_EXPECT_STREQ(BackendKindName(unknown), "unknown");
    CG_EXPECT_STREQ(BackendKindName(BackendKind::D3D12), "d3d12");
}

CG_TEST(rhi_conventions_queue_kinds)
{
    CG_EXPECT_EQ(kQueueKindCount, u32{3});
    CG_EXPECT_STREQ(QueueKindName(QueueKind::Graphics), "graphics");
    CG_EXPECT_STREQ(QueueKindName(QueueKind::Compute), "compute");
    CG_EXPECT_STREQ(QueueKindName(QueueKind::Copy), "copy");
    CG_EXPECT_STREQ(QueueKindName(QueueKind::Count), "unknown");
    CG_EXPECT_EQ(static_cast<u32>(QueueKind::Graphics), u32{0});
}
