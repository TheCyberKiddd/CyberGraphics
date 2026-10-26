#include <cyber/memory/span.h>
#include <cyber/rhi/render_pass.h>
#include <cyber/test/test.h>

#include <limits>

using namespace cyber::rhi;
using cyber::core::ErrorCode;
using cyber::core::u32;
using cyber::memory::Span;

namespace
{

ColorAttachment MakeColor(TextureViewHandle view) noexcept
{
    ColorAttachment attachment{};
    attachment.view = view;
    attachment.loadOp = LoadOp::Clear;
    attachment.storeOp = StoreOp::Store;
    attachment.clearValue = ClearColorValue::Of(0.1f, 0.2f, 0.3f, 1.0f);
    return attachment;
}

TextureViewHandle View(u32 index) noexcept
{
    TextureViewHandle handle;
    handle.index = index;
    handle.generation = 1;
    return handle;
}

}  // namespace

CG_TEST(rhi_render_pass_needs_an_attachment)
{
    // A pass with nothing to write is a caller mistake, not an empty pass.
    const auto status = ValidateRenderPassDesc(RenderPassDesc{});
    CG_EXPECT_FALSE(status);
    if (!status)
    {
        CG_EXPECT_EQ(status.Error().code, ErrorCode::InvalidArgument);
    }
}

CG_TEST(rhi_render_pass_validates_color_attachments)
{
    ColorAttachment attachment = MakeColor(View(1));
    RenderPassDesc desc{};
    desc.colorAttachments = Span<const ColorAttachment>(&attachment, 1);
    CG_EXPECT_TRUE(ValidateRenderPassDesc(desc).IsOk());

    // No view, no attachment.
    ColorAttachment withoutView = MakeColor(TextureViewHandle{});
    RenderPassDesc missing{};
    missing.colorAttachments = Span<const ColorAttachment>(&withoutView, 1);
    CG_EXPECT_FALSE(ValidateRenderPassDesc(missing));

    // An enumerator that is not an operation is rejected rather than treated as
    // "load nothing".
    ColorAttachment badLoad = MakeColor(View(1));
    badLoad.loadOp = LoadOp::Count;
    RenderPassDesc badLoadDesc{};
    badLoadDesc.colorAttachments = Span<const ColorAttachment>(&badLoad, 1);
    CG_EXPECT_FALSE(ValidateRenderPassDesc(badLoadDesc));

    ColorAttachment badStore = MakeColor(View(1));
    badStore.storeOp = StoreOp::Count;
    RenderPassDesc badStoreDesc{};
    badStoreDesc.colorAttachments = Span<const ColorAttachment>(&badStore, 1);
    CG_EXPECT_FALSE(ValidateRenderPassDesc(badStoreDesc));

    // A NaN clear passes every driver and produces a frame nobody can debug.
    ColorAttachment nanClear = MakeColor(View(1));
    nanClear.clearValue.red = std::numeric_limits<float>::quiet_NaN();
    RenderPassDesc nanDesc{};
    nanDesc.colorAttachments = Span<const ColorAttachment>(&nanClear, 1);
    CG_EXPECT_FALSE(ValidateRenderPassDesc(nanDesc));

    // The same view twice is an aliasing hazard inside one pass.
    ColorAttachment duplicated[2] = {MakeColor(View(2)), MakeColor(View(2))};
    RenderPassDesc duplicateDesc{};
    duplicateDesc.colorAttachments = Span<const ColorAttachment>(duplicated, 2);
    CG_EXPECT_FALSE(ValidateRenderPassDesc(duplicateDesc));

    // Two distinct views are fine.
    ColorAttachment distinct[2] = {MakeColor(View(2)), MakeColor(View(3))};
    RenderPassDesc distinctDesc{};
    distinctDesc.colorAttachments = Span<const ColorAttachment>(&distinct[0], 2);
    CG_EXPECT_TRUE(ValidateRenderPassDesc(distinctDesc).IsOk());
}

CG_TEST(rhi_render_pass_validates_resolve_targets)
{
    ColorAttachment attachment = MakeColor(View(1));
    attachment.resolveView = View(1);
    RenderPassDesc self{};
    self.colorAttachments = Span<const ColorAttachment>(&attachment, 1);
    CG_EXPECT_FALSE(ValidateRenderPassDesc(self));

    // A resolve reads the attachment, so discarding the result would resolve
    // undefined data.
    ColorAttachment discarded = MakeColor(View(1));
    discarded.resolveView = View(2);
    discarded.storeOp = StoreOp::DontCare;
    RenderPassDesc discardedDesc{};
    discardedDesc.colorAttachments = Span<const ColorAttachment>(&discarded, 1);
    CG_EXPECT_FALSE(ValidateRenderPassDesc(discardedDesc));

    ColorAttachment resolving = MakeColor(View(1));
    resolving.resolveView = View(2);
    RenderPassDesc resolvingDesc{};
    resolvingDesc.colorAttachments = Span<const ColorAttachment>(&resolving, 1);
    CG_EXPECT_TRUE(ValidateRenderPassDesc(resolvingDesc).IsOk());

    // One view cannot be an attachment of one slot and the resolve target of
    // another: both would be written in the same pass.
    ColorAttachment pair[2] = {MakeColor(View(4)), MakeColor(View(5))};
    pair[0].resolveView = View(5);
    RenderPassDesc pairDesc{};
    pairDesc.colorAttachments = Span<const ColorAttachment>(pair, 2);
    CG_EXPECT_FALSE(ValidateRenderPassDesc(pairDesc));
}

CG_TEST(rhi_render_pass_validates_depth_attachments)
{
    ColorAttachment color = MakeColor(View(1));

    // A depth view without the flag would silently drop the depth buffer.
    RenderPassDesc unflagged{};
    unflagged.colorAttachments = Span<const ColorAttachment>(&color, 1);
    unflagged.depthStencil.view = View(2);
    CG_EXPECT_FALSE(ValidateRenderPassDesc(unflagged));

    // The flag without a view is equally wrong.
    RenderPassDesc viewless{};
    viewless.colorAttachments = Span<const ColorAttachment>(&color, 1);
    viewless.hasDepthStencil = true;
    CG_EXPECT_FALSE(ValidateRenderPassDesc(viewless));

    RenderPassDesc valid{};
    valid.colorAttachments = Span<const ColorAttachment>(&color, 1);
    valid.hasDepthStencil = true;
    valid.depthStencil.view = View(2);
    CG_EXPECT_TRUE(ValidateRenderPassDesc(valid).IsOk());

    // A read-only attachment is never written: clearing it is a write, and
    // discarding it breaks the next pass that reads it.
    RenderPassDesc readOnlyClear = valid;
    readOnlyClear.depthStencil.readOnly = true;
    readOnlyClear.depthStencil.depthLoadOp = LoadOp::Clear;
    CG_EXPECT_FALSE(ValidateRenderPassDesc(readOnlyClear));

    RenderPassDesc readOnlyDiscard = valid;
    readOnlyDiscard.depthStencil.readOnly = true;
    readOnlyDiscard.depthStencil.depthLoadOp = LoadOp::Load;
    readOnlyDiscard.depthStencil.depthStoreOp = StoreOp::DontCare;
    CG_EXPECT_FALSE(ValidateRenderPassDesc(readOnlyDiscard));

    RenderPassDesc readOnly = valid;
    readOnly.depthStencil.readOnly = true;
    readOnly.depthStencil.depthLoadOp = LoadOp::Load;
    readOnly.depthStencil.depthStoreOp = StoreOp::Store;
    CG_EXPECT_TRUE(ValidateRenderPassDesc(readOnly).IsOk());

    // Reverse-Z clears depth to 1, so a clear outside 0..1 is a description bug.
    RenderPassDesc badClear = valid;
    badClear.depthStencil.clearValue.depth = 2.0f;
    CG_EXPECT_FALSE(ValidateRenderPassDesc(badClear));

    RenderPassDesc nanClear = valid;
    nanClear.depthStencil.clearValue.depth = std::numeric_limits<float>::quiet_NaN();
    CG_EXPECT_FALSE(ValidateRenderPassDesc(nanClear));
}

CG_TEST(rhi_render_pass_rejects_shared_color_and_depth_view)
{
    ColorAttachment color = MakeColor(View(7));
    RenderPassDesc desc{};
    desc.colorAttachments = Span<const ColorAttachment>(&color, 1);
    desc.hasDepthStencil = true;
    desc.depthStencil.view = View(7);
    CG_EXPECT_FALSE(ValidateRenderPassDesc(desc));
}

CG_TEST(rhi_render_pass_accepts_a_depth_only_pass)
{
    // A shadow pass writes only depth. It is a legal pass, which matters for
    // Phase 28 and is worth pinning down now.
    RenderPassDesc desc{};
    desc.hasDepthStencil = true;
    desc.depthStencil.view = View(1);
    desc.depthStencil.depthLoadOp = LoadOp::Clear;
    desc.depthStencil.depthStoreOp = StoreOp::Store;
    CG_EXPECT_TRUE(ValidateRenderPassDesc(desc).IsOk());
}

CG_TEST(rhi_render_pass_names_and_ranges)
{
    CG_EXPECT_STREQ(LoadOpName(LoadOp::Load), "load");
    CG_EXPECT_STREQ(LoadOpName(LoadOp::Clear), "clear");
    CG_EXPECT_STREQ(LoadOpName(LoadOp::DontCare), "dont_care");
    CG_EXPECT_STREQ(LoadOpName(LoadOp::Count), "unknown");
    CG_EXPECT_STREQ(StoreOpName(StoreOp::Store), "store");
    CG_EXPECT_STREQ(StoreOpName(StoreOp::DontCare), "dont_care");
    CG_EXPECT_STREQ(StoreOpName(StoreOp::Count), "unknown");

    for (u32 index = 0; index < kLoadOpCount; ++index)
    {
        CG_EXPECT_TRUE(IsValidLoadOp(static_cast<LoadOp>(index)));
    }
    CG_EXPECT_FALSE(IsValidLoadOp(LoadOp::Count));
    for (u32 index = 0; index < kStoreOpCount; ++index)
    {
        CG_EXPECT_TRUE(IsValidStoreOp(static_cast<StoreOp>(index)));
    }
    CG_EXPECT_FALSE(IsValidStoreOp(StoreOp::Count));

    // A default clear value is opaque black for color and the far plane for
    // depth, which is the reverse-Z convention.
    // Named because CG_EXPECT_EQ binds a reference: a member of a temporary would
    // dangle as soon as the full expression ended.
    const ClearDepthStencilValue defaultDepth{};
    CG_EXPECT_EQ(ClearColorValue{}, ClearColorValue::Of(0.0f, 0.0f, 0.0f, 1.0f));
    CG_EXPECT_EQ(defaultDepth.depth, 1.0f);
    CG_EXPECT_TRUE(defaultDepth.IsValid());

    ClearColorValue color = ClearColorValue::Of(0.5f, 0.25f, 0.0f, 1.0f);
    CG_EXPECT_TRUE(color.IsFinite());
    color.green = std::numeric_limits<float>::infinity();
    CG_EXPECT_FALSE(color.IsFinite());
    CG_EXPECT_FALSE(IsFiniteFloat(std::numeric_limits<float>::infinity()));
    CG_EXPECT_FALSE(IsFiniteFloat(std::numeric_limits<float>::quiet_NaN()));
}
