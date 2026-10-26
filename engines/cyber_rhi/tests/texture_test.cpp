#include <cyber/memory/heap_allocator.h>
#include <cyber/rhi/backend.h>
#include <cyber/rhi/device.h>
#include <cyber/rhi/texture.h>
#include <cyber/test/test.h>

#include <utility>

using namespace cyber::rhi;
using cyber::core::ErrorCode;
using cyber::core::u32;
using cyber::core::u64;
using cyber::memory::HeapAllocator;

namespace
{

bool OpenDevice(Device& out, HeapAllocator& heap) noexcept
{
    DeviceDesc desc{};
    desc.backend = NullBackend();
    auto created = Device::Create(desc, heap);
    if (!created)
    {
        return false;
    }
    out = std::move(created.Value());
    return true;
}

TextureDesc MakeTexture(Format format, TextureUsage usage, u32 width, u32 height,
                        u32 mipLevels) noexcept
{
    TextureDesc desc{};
    desc.dimension = TextureDimension::Dim2D;
    desc.format = format;
    desc.usage = usage;
    desc.width = width;
    desc.height = height;
    desc.depth = 1;
    desc.arrayLayers = 1;
    desc.mipLevels = mipLevels;
    desc.sampleCount = 1;
    desc.label = "test texture";
    return desc;
}

TextureDesc MakeDepth(u32 width, u32 height) noexcept
{
    TextureDesc desc = MakeTexture(Format::D32Float, TextureUsage::DepthStencil, width, height, 1);
    return desc;
}

}  // namespace

CG_TEST(rhi_texture_create_and_info)
{
    HeapAllocator heap;
    {
        Device device;
        CG_REQUIRE(OpenDevice(device, heap));

        TextureDesc desc = MakeTexture(
            Format::RGBA8Unorm, TextureUsage::Sampled | TextureUsage::RenderTarget, 256, 256, 9);
        auto created = device.CreateTexture(desc);
        CG_REQUIRE(static_cast<bool>(created));
        const TextureHandle texture = created.Value();

        const TextureInfo info = device.TextureInfoOf(texture);
        CG_EXPECT_TRUE(info.valid);
        CG_EXPECT_EQ(info.dimension, TextureDimension::Dim2D);
        CG_EXPECT_EQ(info.format, Format::RGBA8Unorm);
        CG_EXPECT_EQ(info.usage, TextureUsage::Sampled | TextureUsage::RenderTarget);
        CG_EXPECT_EQ(info.width, u32{256});
        CG_EXPECT_EQ(info.height, u32{256});
        CG_EXPECT_EQ(info.depth, u32{1});
        CG_EXPECT_EQ(info.arrayLayers, u32{1});
        CG_EXPECT_EQ(info.mipLevels, u32{9});
        CG_EXPECT_EQ(info.sampleCount, u32{1});
        CG_EXPECT_EQ(info.sizeBytes, TextureByteSize(desc));
        CG_EXPECT_EQ(device.TextureStateOf(texture, 0, 0), ResourceState::Undefined);

        // A full-range view resolves the caller's zero counts to the texture's
        // shape, so a query reports what the view actually covers.
        TextureViewDesc viewDesc{};
        viewDesc.texture = texture;
        viewDesc.format = Format::RGBA8Unorm;
        viewDesc.dimension = TextureDimension::Dim2D;
        auto view = device.CreateTextureView(viewDesc);
        CG_REQUIRE(static_cast<bool>(view));
        const TextureViewInfo viewInfo = device.TextureViewInfoOf(view.Value());
        CG_EXPECT_TRUE(viewInfo.valid);
        CG_EXPECT_EQ(viewInfo.format, Format::RGBA8Unorm);
        CG_EXPECT_EQ(viewInfo.range.baseMipLevel, u32{0});
        CG_EXPECT_EQ(viewInfo.range.mipLevelCount, u32{9});
        CG_EXPECT_EQ(viewInfo.range.baseArrayLayer, u32{0});
        CG_EXPECT_EQ(viewInfo.range.layerCount, u32{1});
        CG_EXPECT_EQ(viewInfo.range.aspect, TextureAspect::Color);

        CG_EXPECT_TRUE(device.DestroyTextureView(view.Value()));
        CG_EXPECT_TRUE(device.DestroyTexture(texture));
        CG_EXPECT_FALSE(device.TextureInfoOf(texture).valid);
        CG_EXPECT_EQ(device.TextureStateOf(texture, 0, 0), ResourceState::Undefined);
    }
    CG_EXPECT_EQ(heap.Stats().used, cyber::core::usize{0});
}

CG_TEST(rhi_texture_invalid_shapes)
{
    HeapAllocator heap;
    Device device;
    CG_REQUIRE(OpenDevice(device, heap));

    struct Case
    {
        TextureDesc desc;
        ErrorCode code;
    };

    Case cases[] = {
        // No usage at all.
        {MakeTexture(Format::RGBA8Unorm, TextureUsage::None, 16, 16, 1),
         ErrorCode::InvalidArgument},
        // No format.
        {MakeTexture(Format::Count, TextureUsage::Sampled, 16, 16, 1), ErrorCode::InvalidArgument},
    };
    for (const Case& testCase : cases)
    {
        const auto created = device.CreateTexture(testCase.desc);
        CG_EXPECT_FALSE(created);
        CG_EXPECT_EQ(created.Error().code, testCase.code);
    }

    // Zero extents.
    TextureDesc zero = MakeTexture(Format::RGBA8Unorm, TextureUsage::Sampled, 0, 16, 1);
    CG_EXPECT_FALSE(device.CreateTexture(zero));
    zero = MakeTexture(Format::RGBA8Unorm, TextureUsage::Sampled, 16, 0, 1);
    CG_EXPECT_FALSE(device.CreateTexture(zero));
    zero = MakeTexture(Format::RGBA8Unorm, TextureUsage::Sampled, 16, 16, 0);
    CG_EXPECT_FALSE(device.CreateTexture(zero));

    // A 1D texture has height and depth of one, a 2D texture a depth of one,
    // and a 3D texture exactly one layer.
    TextureDesc shape = MakeTexture(Format::RGBA8Unorm, TextureUsage::Sampled, 16, 16, 1);
    shape.dimension = TextureDimension::Dim1D;
    CG_EXPECT_FALSE(device.CreateTexture(shape));
    shape.dimension = TextureDimension::Dim2D;
    shape.depth = 2;
    CG_EXPECT_FALSE(device.CreateTexture(shape));
    shape = MakeTexture(Format::RGBA8Unorm, TextureUsage::Sampled, 16, 16, 1);
    shape.dimension = TextureDimension::Dim3D;
    shape.depth = 16;
    shape.arrayLayers = 2;
    CG_EXPECT_FALSE(device.CreateTexture(shape));

    // A cube is square and its layer count is a multiple of six.
    TextureDesc cube = MakeTexture(Format::RGBA8Unorm, TextureUsage::Sampled, 32, 16, 1);
    cube.dimension = TextureDimension::Cube;
    cube.arrayLayers = 6;
    CG_EXPECT_FALSE(device.CreateTexture(cube));
    cube = MakeTexture(Format::RGBA8Unorm, TextureUsage::Sampled, 32, 32, 1);
    cube.dimension = TextureDimension::Cube;
    cube.arrayLayers = 8;
    CG_EXPECT_FALSE(device.CreateTexture(cube));
    cube.arrayLayers = 12;
    CG_EXPECT_TRUE(static_cast<bool>(device.CreateTexture(cube)));

    // The mip chain cannot exceed what the extent allows: 16 has five levels.
    TextureDesc mips = MakeTexture(Format::RGBA8Unorm, TextureUsage::Sampled, 16, 16, 6);
    const auto tooManyMips = device.CreateTexture(mips);
    CG_EXPECT_FALSE(tooManyMips);
    CG_EXPECT_EQ(tooManyMips.Error().code, ErrorCode::InvalidArgument);
    mips.mipLevels = 5;
    CG_EXPECT_TRUE(static_cast<bool>(device.CreateTexture(mips)));

    // Sample counts are powers of two up to eight, one mip, 2D only, and never
    // block compressed.
    TextureDesc msaa = MakeTexture(Format::RGBA8Unorm, TextureUsage::RenderTarget, 64, 64, 1);
    msaa.sampleCount = 3;
    CG_EXPECT_FALSE(device.CreateTexture(msaa));
    msaa.sampleCount = 4;
    CG_EXPECT_TRUE(static_cast<bool>(device.CreateTexture(msaa)));
    msaa.mipLevels = 2;
    CG_EXPECT_FALSE(device.CreateTexture(msaa));
    msaa = MakeTexture(Format::BC1Unorm, TextureUsage::Sampled, 64, 64, 1);
    msaa.sampleCount = 4;
    CG_EXPECT_FALSE(device.CreateTexture(msaa));
    msaa = MakeTexture(Format::RGBA8Unorm, TextureUsage::RenderTarget, 64, 64, 1);
    msaa.dimension = TextureDimension::Cube;
    msaa.arrayLayers = 6;
    msaa.sampleCount = 4;
    CG_EXPECT_FALSE(device.CreateTexture(msaa));

    // A compressed format is not a render target, and a color format is not a
    // depth attachment.
    TextureDesc wrongUsage = MakeTexture(Format::BC1Unorm, TextureUsage::RenderTarget, 64, 64, 1);
    CG_EXPECT_FALSE(device.CreateTexture(wrongUsage));
    wrongUsage = MakeTexture(Format::RGBA8Unorm, TextureUsage::DepthStencil, 64, 64, 1);
    CG_EXPECT_FALSE(device.CreateTexture(wrongUsage));

    // Both attachment kinds at once is a contradiction: a format is one or the
    // other.
    wrongUsage = MakeTexture(Format::D32Float,
                             TextureUsage::DepthStencil | TextureUsage::RenderTarget, 64, 64, 1);
    CG_EXPECT_FALSE(device.CreateTexture(wrongUsage));

    // A depth texture may be an attachment, sampled, or a copy target. Usage
    // alone decides which, and any of the three is accepted.
    TextureDesc depthOnly = MakeDepth(64, 64);
    CG_EXPECT_TRUE(static_cast<bool>(device.CreateTexture(depthOnly)));
    const TextureDesc sampledDepth =
        MakeTexture(Format::D32Float, TextureUsage::Sampled, 64, 64, 1);
    CG_EXPECT_TRUE(static_cast<bool>(device.CreateTexture(sampledDepth)));
    const TextureDesc copiedDepth =
        MakeTexture(Format::D32Float, TextureUsage::CopyDestination, 64, 64, 1);
    CG_EXPECT_TRUE(static_cast<bool>(device.CreateTexture(copiedDepth)));
}

CG_TEST(rhi_texture_limits)
{
    HeapAllocator heap;
    Device device;
    CG_REQUIRE(OpenDevice(device, heap));
    const Limits limits = device.Capabilities().limits;

    // Above a capability limit is `OutOfRange`, distinct from a malformed shape.
    TextureDesc desc =
        MakeTexture(Format::RGBA8Unorm, TextureUsage::Sampled, limits.maxTexture2D + 1, 16, 1);
    auto created = device.CreateTexture(desc);
    CG_EXPECT_FALSE(created);
    CG_EXPECT_EQ(created.Error().code, ErrorCode::OutOfRange);

    desc = MakeTexture(Format::RGBA8Unorm, TextureUsage::Sampled, 16, limits.maxTexture2D + 1, 1);
    created = device.CreateTexture(desc);
    CG_EXPECT_FALSE(created);
    CG_EXPECT_EQ(created.Error().code, ErrorCode::OutOfRange);

    desc = MakeTexture(Format::RGBA8Unorm, TextureUsage::Sampled, 16, 1, 1);
    desc.dimension = TextureDimension::Dim1D;
    desc.width = limits.maxTexture1D + 1;
    created = device.CreateTexture(desc);
    CG_EXPECT_FALSE(created);
    CG_EXPECT_EQ(created.Error().code, ErrorCode::OutOfRange);

    desc = MakeTexture(Format::RGBA8Unorm, TextureUsage::Sampled, 16, 16, 1);
    desc.dimension = TextureDimension::Dim3D;
    desc.depth = limits.maxTexture3D + 1;
    created = device.CreateTexture(desc);
    CG_EXPECT_FALSE(created);
    CG_EXPECT_EQ(created.Error().code, ErrorCode::OutOfRange);

    desc = MakeTexture(Format::RGBA8Unorm, TextureUsage::Sampled, 16, 16, 1);
    desc.arrayLayers = limits.maxTextureArrayLayers + 1;
    created = device.CreateTexture(desc);
    CG_EXPECT_FALSE(created);
    CG_EXPECT_EQ(created.Error().code, ErrorCode::OutOfRange);

    // The limit itself is accepted: an off-by-one in the comparison would show
    // up here as a false rejection.
    desc = MakeTexture(Format::RGBA8Unorm, TextureUsage::Sampled, limits.maxTexture2D,
                       limits.maxTexture2D, 1);
    CG_EXPECT_TRUE(static_cast<bool>(device.CreateTexture(desc)));
}

CG_TEST(rhi_texture_views)
{
    HeapAllocator heap;
    Device device;
    CG_REQUIRE(OpenDevice(device, heap));

    auto base = device.CreateTexture(MakeTexture(
        Format::RGBA8Unorm, TextureUsage::Sampled | TextureUsage::RenderTarget, 64, 64, 7));
    CG_REQUIRE(static_cast<bool>(base));
    const TextureHandle texture = base.Value();

    // The sRGB encoding of the same layout is a legal view.
    TextureViewDesc viewDesc{};
    viewDesc.texture = texture;
    viewDesc.format = Format::RGBA8UnormSrgb;
    auto srgbView = device.CreateTextureView(viewDesc);
    CG_EXPECT_TRUE(static_cast<bool>(srgbView));
    if (srgbView)
    {
        CG_EXPECT_EQ(device.TextureViewInfoOf(srgbView.Value()).format, Format::RGBA8UnormSrgb);
        CG_EXPECT_TRUE(device.DestroyTextureView(srgbView.Value()));
    }

    // A different layout is not.
    viewDesc.format = Format::R8Unorm;
    const auto wrongFormat = device.CreateTextureView(viewDesc);
    CG_EXPECT_FALSE(wrongFormat);
    CG_EXPECT_EQ(wrongFormat.Error().code, ErrorCode::InvalidArgument);

    // A range that leaves the texture is rejected, and so is a dimension the
    // texture cannot expose.
    viewDesc.format = Format::RGBA8Unorm;
    viewDesc.range.baseMipLevel = 7;
    const auto mipPastEnd = device.CreateTextureView(viewDesc);
    CG_EXPECT_FALSE(mipPastEnd);
    CG_EXPECT_EQ(mipPastEnd.Error().code, ErrorCode::InvalidArgument);

    viewDesc.range = SubresourceRange{};
    viewDesc.range.mipLevelCount = 8;
    const auto tooManyMips = device.CreateTextureView(viewDesc);
    CG_EXPECT_FALSE(tooManyMips);
    CG_EXPECT_EQ(tooManyMips.Error().code, ErrorCode::InvalidArgument);

    viewDesc.range = SubresourceRange{};
    viewDesc.dimension = TextureDimension::Dim1D;
    const auto wrongDimension = device.CreateTextureView(viewDesc);
    CG_EXPECT_FALSE(wrongDimension);
    CG_EXPECT_EQ(wrongDimension.Error().code, ErrorCode::InvalidArgument);

    // A color texture has no depth or stencil aspect.
    viewDesc.dimension = TextureDimension::Dim2D;
    viewDesc.range.aspect = TextureAspect::Depth;
    const auto depthAspect = device.CreateTextureView(viewDesc);
    CG_EXPECT_FALSE(depthAspect);
    CG_EXPECT_EQ(depthAspect.Error().code, ErrorCode::InvalidArgument);

    // A cube view needs a square texture and a multiple of six layers.
    auto square =
        device.CreateTexture(MakeTexture(Format::RGBA8Unorm, TextureUsage::Sampled, 64, 64, 1));
    CG_REQUIRE(static_cast<bool>(square));
    TextureViewDesc cubeView{};
    cubeView.texture = square.Value();
    cubeView.dimension = TextureDimension::Cube;
    cubeView.range.layerCount = 6;
    const auto notEnoughLayers = device.CreateTextureView(cubeView);
    CG_EXPECT_FALSE(notEnoughLayers);
    CG_EXPECT_EQ(notEnoughLayers.Error().code, ErrorCode::InvalidArgument);

    TextureDesc layered = MakeTexture(Format::RGBA8Unorm, TextureUsage::Sampled, 64, 64, 1);
    layered.arrayLayers = 6;
    auto layerTexture = device.CreateTexture(layered);
    CG_REQUIRE(static_cast<bool>(layerTexture));
    cubeView.texture = layerTexture.Value();
    cubeView.range.layerCount = 6;
    const auto cube = device.CreateTextureView(cubeView);
    CG_EXPECT_TRUE(static_cast<bool>(cube));
    if (cube)
    {
        CG_EXPECT_EQ(device.TextureViewInfoOf(cube.Value()).dimension, TextureDimension::Cube);
    }

    // A depth texture exposes the aspects its format stores.
    auto depth = device.CreateTexture(MakeDepth(32, 32));
    CG_REQUIRE(static_cast<bool>(depth));
    TextureViewDesc depthView{};
    depthView.texture = depth.Value();
    depthView.format = Format::D32Float;
    depthView.range.aspect = TextureAspect::Stencil;
    const auto stencil = device.CreateTextureView(depthView);
    CG_EXPECT_FALSE(stencil);
    CG_EXPECT_EQ(stencil.Error().code, ErrorCode::InvalidArgument);
    depthView.range.aspect = TextureAspect::Depth;
    CG_EXPECT_TRUE(static_cast<bool>(device.CreateTextureView(depthView)));

    auto combined = device.CreateTexture(MakeTexture(
        Format::D24UnormS8Uint, TextureUsage::DepthStencil | TextureUsage::Sampled, 32, 32, 1));
    CG_REQUIRE(static_cast<bool>(combined));
    depthView.texture = combined.Value();
    depthView.format = Format::D24UnormS8Uint;
    depthView.range.aspect = TextureAspect::DepthStencil;
    CG_EXPECT_TRUE(static_cast<bool>(device.CreateTextureView(depthView)));
    depthView.range.aspect = TextureAspect::Stencil;
    CG_EXPECT_TRUE(static_cast<bool>(device.CreateTextureView(depthView)));

    // A view of a stale texture is rejected.
    const TextureHandle stale = texture;
    CG_EXPECT_TRUE(device.DestroyTexture(stale));
    viewDesc.texture = stale;
    const auto staleView = device.CreateTextureView(viewDesc);
    CG_EXPECT_FALSE(staleView);
    CG_EXPECT_EQ(staleView.Error().code, ErrorCode::InvalidArgument);
}

CG_TEST(rhi_texture_view_lifetime)
{
    HeapAllocator heap;
    Device device;
    CG_REQUIRE(OpenDevice(device, heap));

    auto created =
        device.CreateTexture(MakeTexture(Format::RGBA8Unorm, TextureUsage::Sampled, 32, 32, 3));
    CG_REQUIRE(static_cast<bool>(created));
    const TextureHandle texture = created.Value();

    TextureViewDesc viewDesc{};
    viewDesc.texture = texture;
    auto first = device.CreateTextureView(viewDesc);
    auto second = device.CreateTextureView(viewDesc);
    CG_REQUIRE(static_cast<bool>(first));
    CG_REQUIRE(static_cast<bool>(second));

    // Destroying one view leaves the other and the texture alive.
    CG_EXPECT_TRUE(device.DestroyTextureView(first.Value()));
    CG_EXPECT_FALSE(device.TextureViewInfoOf(first.Value()).valid);
    CG_EXPECT_TRUE(device.TextureViewInfoOf(second.Value()).valid);

    // Destroying the texture retires every view of it: neither view resolves,
    // and destroying one again is a stale-handle error rather than a crash.
    CG_EXPECT_TRUE(device.DestroyTexture(texture));
    CG_EXPECT_FALSE(device.TextureViewInfoOf(second.Value()).valid);
    const auto destroyRetired = device.DestroyTextureView(second.Value());
    CG_EXPECT_FALSE(destroyRetired);
    CG_EXPECT_EQ(destroyRetired.Error().code, ErrorCode::InvalidArgument);

    // The retired view's slot is reusable. A new view gets a new generation and
    // the old handle does not alias it.
    auto other =
        device.CreateTexture(MakeTexture(Format::RGBA8Unorm, TextureUsage::Sampled, 32, 32, 1));
    CG_REQUIRE(static_cast<bool>(other));
    TextureViewDesc otherDesc{};
    otherDesc.texture = other.Value();
    auto replacement = device.CreateTextureView(otherDesc);
    CG_REQUIRE(static_cast<bool>(replacement));
    CG_EXPECT_TRUE(device.TextureViewInfoOf(replacement.Value()).valid);
    CG_EXPECT_FALSE(device.TextureViewInfoOf(second.Value()).valid);
}

CG_TEST(rhi_texture_state_per_subresource)
{
    HeapAllocator heap;
    Device device;
    CG_REQUIRE(OpenDevice(device, heap));

    TextureDesc desc = MakeTexture(Format::RGBA8Unorm, TextureUsage::Sampled, 8, 8, 4);
    desc.arrayLayers = 2;
    auto created = device.CreateTexture(desc);
    CG_REQUIRE(static_cast<bool>(created));
    const TextureHandle texture = created.Value();

    for (u32 mip = 0; mip < 4; ++mip)
    {
        for (u32 layer = 0; layer < 2; ++layer)
        {
            CG_EXPECT_EQ(device.TextureStateOf(texture, mip, layer), ResourceState::Undefined);
        }
    }
    // Out-of-range indices answer Undefined rather than reading past the state
    // array.
    CG_EXPECT_EQ(device.TextureStateOf(texture, 4, 0), ResourceState::Undefined);
    CG_EXPECT_EQ(device.TextureStateOf(texture, 0, 2), ResourceState::Undefined);
    CG_EXPECT_EQ(device.TextureStateOf(TextureHandle{}, 0, 0), ResourceState::Undefined);
}

CG_TEST(rhi_texture_storage_usage)
{
    HeapAllocator heap;
    Device device;
    CG_REQUIRE(OpenDevice(device, heap));

    TextureDesc desc =
        MakeTexture(Format::RGBA8Unorm, TextureUsage::Storage | TextureUsage::Sampled, 32, 32, 1);
    CG_EXPECT_TRUE(static_cast<bool>(device.CreateTexture(desc)));

    // Storage textures are single-mip, single-sample, and linear.
    desc.mipLevels = 2;
    const auto mips = device.CreateTexture(desc);
    CG_EXPECT_FALSE(mips);
    CG_EXPECT_EQ(mips.Error().code, ErrorCode::InvalidArgument);

    desc = MakeTexture(Format::RGBA8UnormSrgb, TextureUsage::Storage, 32, 32, 1);
    const auto srgb = device.CreateTexture(desc);
    CG_EXPECT_FALSE(srgb);
    CG_EXPECT_EQ(srgb.Error().code, ErrorCode::InvalidArgument);

    desc = MakeTexture(Format::BC7Unorm, TextureUsage::Storage, 32, 32, 1);
    const auto compressed = device.CreateTexture(desc);
    CG_EXPECT_FALSE(compressed);
    CG_EXPECT_EQ(compressed.Error().code, ErrorCode::InvalidArgument);

    desc = MakeTexture(Format::D32Float, TextureUsage::Storage, 32, 32, 1);
    const auto depth = device.CreateTexture(desc);
    CG_EXPECT_FALSE(depth);
    CG_EXPECT_EQ(depth.Error().code, ErrorCode::InvalidArgument);
}

CG_TEST(rhi_texture_byte_size)
{
    // 4x4 RGBA8: one 4-byte row padded to the 256-byte pitch, four rows.
    TextureDesc desc = MakeTexture(Format::RGBA8Unorm, TextureUsage::Sampled, 4, 4, 1);
    CG_EXPECT_EQ(TextureByteSize(desc), u64{1024});

    // 2x2 with three mips: 256*2 + 256*1 + 256*1.
    desc = MakeTexture(Format::RGBA8Unorm, TextureUsage::Sampled, 2, 2, 3);
    CG_EXPECT_EQ(TextureByteSize(desc), u64{1024});

    // Block-compressed rows are four texels wide, so a 8x8 BC1 mip is two
    // 8-byte blocks per row, padded to 256, two rows.
    desc = MakeTexture(Format::BC1Unorm, TextureUsage::Sampled, 8, 8, 1);
    CG_EXPECT_EQ(TextureByteSize(desc), u64{512});

    // Alignment of one byte is the packed size.
    desc = MakeTexture(Format::RGBA8Unorm, TextureUsage::Sampled, 4, 4, 1);
    CG_EXPECT_EQ(TextureByteSize(desc, 1), u64{64});

    // Array layers multiply the whole chain; a 3D texture's depth multiplies
    // each level.
    desc = MakeTexture(Format::RGBA8Unorm, TextureUsage::Sampled, 4, 4, 1);
    desc.arrayLayers = 3;
    CG_EXPECT_EQ(TextureByteSize(desc, 1), u64{192});
    desc.arrayLayers = 1;
    desc.dimension = TextureDimension::Dim3D;
    desc.depth = 3;
    CG_EXPECT_EQ(TextureByteSize(desc, 1), u64{192});

    // An invalid descriptor has no size instead of a wrapped one.
    desc = MakeTexture(Format::RGBA8Unorm, TextureUsage::Sampled, 0, 4, 1);
    CG_EXPECT_EQ(TextureByteSize(desc), u64{0});
    desc = MakeTexture(Format::Count, TextureUsage::Sampled, 4, 4, 1);
    CG_EXPECT_EQ(TextureByteSize(desc), u64{0});
    desc = MakeTexture(Format::RGBA8Unorm, TextureUsage::Sampled, 4, 4, 1);
    CG_EXPECT_EQ(TextureByteSize(desc, 0), u64{0});

    // MaxMipLevels is the count that halves every dimension to one.
    CG_EXPECT_EQ(MaxMipLevels(TextureDimension::Dim1D, 2048, 1, 1), u32{12});
    CG_EXPECT_EQ(MaxMipLevels(TextureDimension::Dim2D, 1920, 1080, 1), u32{11});
    CG_EXPECT_EQ(MaxMipLevels(TextureDimension::Dim3D, 64, 64, 64), u32{7});
    CG_EXPECT_EQ(MaxMipLevels(TextureDimension::Dim2D, 0, 16, 1), u32{0});
}
