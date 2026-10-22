#include <cyber/math/color.h>
#include <cyber/memory/heap_allocator.h>
#include <cyber/software/framebuffer.h>
#include <cyber/test/test.h>

#include <limits>
#include <utility>

using namespace cyber::software;
using namespace cyber::memory;
using cyber::math::Color;

namespace
{

bool ChannelNear(f32 actual, f32 expected)
{
    const f32 difference = actual > expected ? actual - expected : expected - actual;
    return difference <= (1.0f / 255.0f) + 1.0e-5f;
}

}  // namespace

CG_TEST(software_framebuffer_format_sizes)
{
    CG_EXPECT_EQ(PixelSizeBytes(PixelFormat::RGBA8Unorm), usize{4});
    CG_EXPECT_EQ(PixelSizeBytes(PixelFormat::RGBA32F), usize{16});
    CG_EXPECT_TRUE(IsValidPixelFormat(PixelFormat::RGBA8Unorm));
    CG_EXPECT_TRUE(IsValidPixelFormat(PixelFormat::RGBA32F));
    CG_EXPECT_FALSE(IsValidPixelFormat(static_cast<PixelFormat>(42)));
    CG_EXPECT_EQ(PixelSizeBytes(static_cast<PixelFormat>(42)), usize{0});
}

CG_TEST(software_framebuffer_quantize_and_srgb)
{
    CG_EXPECT_EQ(QuantizeUnorm8(0.0f), u8{0});
    CG_EXPECT_EQ(QuantizeUnorm8(-1.0f), u8{0});
    CG_EXPECT_EQ(QuantizeUnorm8(0.5f), u8{128});
    CG_EXPECT_EQ(QuantizeUnorm8(1.0f), u8{255});
    CG_EXPECT_EQ(QuantizeUnorm8(2.0f), u8{255});

    const f32 nan = std::numeric_limits<f32>::quiet_NaN();
    CG_EXPECT_EQ(QuantizeUnorm8(nan), u8{0});
    CG_EXPECT_EQ(EncodeSrgb8(nan), u8{0});
    CG_EXPECT_EQ(EncodeSrgb8(-1.0f), u8{0});
    CG_EXPECT_EQ(EncodeSrgb8(0.0f), u8{0});
    CG_EXPECT_EQ(EncodeSrgb8(1.0f), u8{255});
    // 0.5 linear encodes to about 0.7354 sRGB.
    CG_EXPECT_EQ(EncodeSrgb8(0.5f), u8{188});
}

CG_TEST(software_framebuffer_create_validation)
{
    HeapAllocator heap;

    FramebufferDesc desc{};
    desc.width = 0;
    desc.height = 4;
    CG_EXPECT_FALSE(static_cast<bool>(Framebuffer::Create(desc, heap)));

    desc.width = 4;
    desc.height = 0;
    CG_EXPECT_FALSE(static_cast<bool>(Framebuffer::Create(desc, heap)));

    desc.width = -1;
    desc.height = 4;
    CG_EXPECT_FALSE(static_cast<bool>(Framebuffer::Create(desc, heap)));

    desc.width = 20000;
    desc.height = 1;
    CG_EXPECT_FALSE(static_cast<bool>(Framebuffer::Create(desc, heap)));

    desc.width = 4;
    desc.height = 4;
    desc.strideBytes = 8;  // Tight stride is 16 bytes.
    CG_EXPECT_FALSE(static_cast<bool>(Framebuffer::Create(desc, heap)));

    desc.format = PixelFormat::RGBA32F;
    desc.width = 1;
    desc.height = 1;
    desc.strideBytes = 18;  // Not a multiple of alignof(f32).
    CG_EXPECT_FALSE(static_cast<bool>(Framebuffer::Create(desc, heap)));

    desc.format = static_cast<PixelFormat>(7);
    desc.strideBytes = 0;
    CG_EXPECT_FALSE(static_cast<bool>(Framebuffer::Create(desc, heap)));

    CG_EXPECT_EQ(heap.Stats().used, usize{0});
}

CG_TEST(software_framebuffer_clear_and_pixels)
{
    HeapAllocator heap;
    FramebufferDesc desc{};
    desc.width = 4;
    desc.height = 3;

    auto created = Framebuffer::Create(desc, heap);
    CG_REQUIRE(static_cast<bool>(created));
    Framebuffer framebuffer = std::move(created.Value());
    CG_EXPECT_TRUE(framebuffer.IsValid());
    CG_EXPECT_EQ(framebuffer.Width(), i32{4});
    CG_EXPECT_EQ(framebuffer.Height(), i32{3});
    CG_EXPECT_EQ(framebuffer.StrideBytes(), usize{16});
    CG_EXPECT_EQ(framebuffer.DataSizeBytes(), usize{48});
    CG_EXPECT_TRUE(framebuffer.Data() != nullptr);
    CG_EXPECT_TRUE(framebuffer.Row(2) == framebuffer.Data() + 32);
    CG_EXPECT_TRUE(framebuffer.Row(3) == nullptr);
    CG_EXPECT_TRUE(framebuffer.Row(-1) == nullptr);

    framebuffer.Clear(Color{0.25f, 0.5f, 0.75f, 1.0f});
    const Color pixel = framebuffer.GetPixel(2, 2);
    CG_EXPECT_TRUE(ChannelNear(pixel.r, 64.0f / 255.0f));
    CG_EXPECT_TRUE(ChannelNear(pixel.g, 128.0f / 255.0f));
    CG_EXPECT_TRUE(ChannelNear(pixel.b, 191.0f / 255.0f));
    CG_EXPECT_TRUE(ChannelNear(pixel.a, 1.0f));

    framebuffer.SetPixel(0, 0, Color{1.0f, 0.0f, 0.0f, 0.5f});
    const Color red = framebuffer.GetPixel(0, 0);
    CG_EXPECT_TRUE(ChannelNear(red.r, 1.0f));
    CG_EXPECT_TRUE(ChannelNear(red.g, 0.0f));
    CG_EXPECT_TRUE(ChannelNear(red.a, 128.0f / 255.0f));

    // Out-of-range access is safe.
    framebuffer.SetPixel(-1, 0, Color{1.0f, 1.0f, 1.0f});
    framebuffer.SetPixel(4, 0, Color{1.0f, 1.0f, 1.0f});
    const Color outside = framebuffer.GetPixel(4, 0);
    CG_EXPECT_EQ(outside.r, 0.0f);
    CG_EXPECT_EQ(outside.a, 0.0f);
    CG_EXPECT_FALSE(framebuffer.Contains(4, 0));
    CG_EXPECT_TRUE(framebuffer.Contains(3, 2));
}

CG_TEST(software_framebuffer_rect_clipping)
{
    HeapAllocator heap;
    FramebufferDesc desc{};
    desc.width = 4;
    desc.height = 4;

    auto created = Framebuffer::Create(desc, heap);
    CG_REQUIRE(static_cast<bool>(created));
    Framebuffer framebuffer = std::move(created.Value());
    framebuffer.Clear(Color{0.0f, 0.0f, 0.0f, 1.0f});

    framebuffer.ClearRect(-1, -1, 3, 3, Color{1.0f, 1.0f, 1.0f, 1.0f});
    CG_EXPECT_TRUE(ChannelNear(framebuffer.GetPixel(0, 0).r, 1.0f));
    CG_EXPECT_TRUE(ChannelNear(framebuffer.GetPixel(1, 1).r, 1.0f));
    CG_EXPECT_TRUE(ChannelNear(framebuffer.GetPixel(2, 2).r, 0.0f));
    CG_EXPECT_TRUE(ChannelNear(framebuffer.GetPixel(3, 0).r, 0.0f));

    framebuffer.Clear(Color{0.0f, 0.0f, 0.0f, 1.0f});
    framebuffer.ClearRect(2, 2, 10, 10, Color{0.5f, 0.5f, 0.5f, 1.0f});
    CG_EXPECT_TRUE(ChannelNear(framebuffer.GetPixel(3, 3).r, 0.5f));
    CG_EXPECT_TRUE(ChannelNear(framebuffer.GetPixel(1, 3).r, 0.0f));
    CG_EXPECT_TRUE(ChannelNear(framebuffer.GetPixel(3, 1).r, 0.0f));

    framebuffer.ClearRect(0, 0, -2, 4, Color{1.0f, 1.0f, 1.0f, 1.0f});
    framebuffer.ClearRect(0, 0, 2, -4, Color{1.0f, 1.0f, 1.0f, 1.0f});
    CG_EXPECT_TRUE(ChannelNear(framebuffer.GetPixel(3, 3).r, 0.5f));
}

CG_TEST(software_framebuffer_stride_padding_stays_zero)
{
    HeapAllocator heap;
    FramebufferDesc desc{};
    desc.width = 3;
    desc.height = 2;
    desc.strideBytes = 16;  // Tight stride is 12; four padding bytes per row.

    auto created = Framebuffer::Create(desc, heap);
    CG_REQUIRE(static_cast<bool>(created));
    Framebuffer framebuffer = std::move(created.Value());
    CG_EXPECT_EQ(framebuffer.StrideBytes(), usize{16});

    framebuffer.Clear(Color{1.0f, 1.0f, 1.0f, 1.0f});
    for (i32 row = 0; row < 2; ++row)
    {
        const u8* bytes = framebuffer.Row(row);
        for (usize index = 12; index < 16; ++index)
        {
            CG_EXPECT_EQ(bytes[index], u8{0});
        }
    }

    framebuffer.SetPixel(2, 1, Color{0.0f, 0.0f, 0.0f, 0.0f});
    const u8* secondRow = framebuffer.Row(1);
    for (usize index = 12; index < 16; ++index)
    {
        CG_EXPECT_EQ(secondRow[index], u8{0});
    }
}

CG_TEST(software_framebuffer_float_roundtrip)
{
    HeapAllocator heap;
    FramebufferDesc desc{};
    desc.width = 2;
    desc.height = 1;
    desc.format = PixelFormat::RGBA32F;

    auto created = Framebuffer::Create(desc, heap);
    CG_REQUIRE(static_cast<bool>(created));
    Framebuffer framebuffer = std::move(created.Value());
    CG_EXPECT_EQ(framebuffer.StrideBytes(), usize{32});
    CG_EXPECT_EQ(framebuffer.DataSizeBytes(), usize{32});

    const Color color{0.1f, 0.2f, 0.3f, 0.4f};
    framebuffer.SetPixel(1, 0, color);
    const Color readBack = framebuffer.GetPixel(1, 0);
    CG_EXPECT_EQ(readBack.r, color.r);
    CG_EXPECT_EQ(readBack.g, color.g);
    CG_EXPECT_EQ(readBack.b, color.b);
    CG_EXPECT_EQ(readBack.a, color.a);

    // Padded rows keep the float alignment rule.
    FramebufferDesc padded = desc;
    padded.strideBytes = 36;
    auto paddedResult = Framebuffer::Create(padded, heap);
    CG_REQUIRE(static_cast<bool>(paddedResult));
    CG_EXPECT_EQ(paddedResult.Value().StrideBytes(), usize{36});
}

CG_TEST(software_framebuffer_blit_clips)
{
    HeapAllocator heap;

    FramebufferDesc sourceDesc{};
    sourceDesc.width = 2;
    sourceDesc.height = 2;
    auto sourceResult = Framebuffer::Create(sourceDesc, heap);
    CG_REQUIRE(static_cast<bool>(sourceResult));
    Framebuffer source = std::move(sourceResult.Value());
    source.SetPixel(0, 0, Color{1.0f, 0.0f, 0.0f, 1.0f});
    source.SetPixel(1, 0, Color{0.0f, 1.0f, 0.0f, 1.0f});
    source.SetPixel(0, 1, Color{0.0f, 0.0f, 1.0f, 1.0f});
    source.SetPixel(1, 1, Color{1.0f, 1.0f, 1.0f, 1.0f});

    FramebufferDesc destinationDesc{};
    destinationDesc.width = 4;
    destinationDesc.height = 4;
    auto destinationResult = Framebuffer::Create(destinationDesc, heap);
    CG_REQUIRE(static_cast<bool>(destinationResult));
    Framebuffer destination = std::move(destinationResult.Value());

    destination.Clear(Color{0.0f, 0.0f, 0.0f, 1.0f});
    destination.Blit(source, 1, 1);
    CG_EXPECT_TRUE(ChannelNear(destination.GetPixel(1, 1).r, 1.0f));
    CG_EXPECT_TRUE(ChannelNear(destination.GetPixel(2, 1).g, 1.0f));
    CG_EXPECT_TRUE(ChannelNear(destination.GetPixel(1, 2).b, 1.0f));
    CG_EXPECT_TRUE(ChannelNear(destination.GetPixel(2, 2).r, 1.0f));
    CG_EXPECT_TRUE(ChannelNear(destination.GetPixel(3, 1).r, 0.0f));
    CG_EXPECT_TRUE(ChannelNear(destination.GetPixel(0, 0).r, 0.0f));

    destination.Clear(Color{0.0f, 0.0f, 0.0f, 1.0f});
    destination.Blit(source, -1, 0);
    CG_EXPECT_TRUE(ChannelNear(destination.GetPixel(0, 0).g, 1.0f));
    CG_EXPECT_TRUE(ChannelNear(destination.GetPixel(0, 1).r, 1.0f));
    CG_EXPECT_TRUE(ChannelNear(destination.GetPixel(1, 0).r, 0.0f));

    // A blit that lands entirely outside is a no-op.
    destination.Clear(Color{0.0f, 0.0f, 0.0f, 1.0f});
    destination.Blit(source, 10, 10);
    destination.Blit(source, -10, 10);
    CG_EXPECT_TRUE(ChannelNear(destination.GetPixel(0, 0).r, 0.0f));
    CG_EXPECT_TRUE(ChannelNear(destination.GetPixel(3, 3).r, 0.0f));
}

CG_TEST(software_framebuffer_format_conversion)
{
    HeapAllocator heap;

    FramebufferDesc floatDesc{};
    floatDesc.width = 2;
    floatDesc.height = 1;
    floatDesc.format = PixelFormat::RGBA32F;
    auto floatResult = Framebuffer::Create(floatDesc, heap);
    CG_REQUIRE(static_cast<bool>(floatResult));
    Framebuffer floatBuffer = std::move(floatResult.Value());
    floatBuffer.SetPixel(0, 0, Color{0.5f, 0.25f, 0.75f, 1.0f});
    floatBuffer.SetPixel(1, 0, Color{0.0f, 1.0f, 0.0f, 0.5f});

    FramebufferDesc byteDesc{};
    byteDesc.width = 2;
    byteDesc.height = 1;
    auto byteResult = Framebuffer::Create(byteDesc, heap);
    CG_REQUIRE(static_cast<bool>(byteResult));
    Framebuffer byteBuffer = std::move(byteResult.Value());

    CG_EXPECT_TRUE(static_cast<bool>(Framebuffer::Convert(floatBuffer, byteBuffer)));
    const Color first = byteBuffer.GetPixel(0, 0);
    CG_EXPECT_TRUE(ChannelNear(first.r, 0.5f));
    CG_EXPECT_TRUE(ChannelNear(first.g, 0.25f));
    CG_EXPECT_TRUE(ChannelNear(first.b, 0.75f));
    CG_EXPECT_TRUE(ChannelNear(first.a, 1.0f));
    const Color second = byteBuffer.GetPixel(1, 0);
    CG_EXPECT_TRUE(ChannelNear(second.g, 1.0f));
    CG_EXPECT_TRUE(ChannelNear(second.a, 128.0f / 255.0f));

    auto roundTripResult = Framebuffer::Create(floatDesc, heap);
    CG_REQUIRE(static_cast<bool>(roundTripResult));
    Framebuffer roundTrip = std::move(roundTripResult.Value());
    CG_EXPECT_TRUE(static_cast<bool>(Framebuffer::Convert(byteBuffer, roundTrip)));
    const Color roundTripPixel = roundTrip.GetPixel(0, 0);
    CG_EXPECT_TRUE(ChannelNear(roundTripPixel.r, 0.5f));
    CG_EXPECT_TRUE(ChannelNear(roundTripPixel.g, 0.25f));
    CG_EXPECT_TRUE(ChannelNear(roundTripPixel.b, 0.75f));

    FramebufferDesc mismatchDesc{};
    mismatchDesc.width = 3;
    mismatchDesc.height = 1;
    auto mismatchResult = Framebuffer::Create(mismatchDesc, heap);
    CG_REQUIRE(static_cast<bool>(mismatchResult));
    Framebuffer mismatch = std::move(mismatchResult.Value());
    const auto status = Framebuffer::Convert(floatBuffer, mismatch);
    CG_EXPECT_FALSE(static_cast<bool>(status));
    CG_EXPECT_TRUE(status.Error().code == cyber::core::ErrorCode::InvalidArgument);

    CG_EXPECT_TRUE(static_cast<bool>(Framebuffer::Convert(floatBuffer, floatBuffer)));
}

CG_TEST(software_framebuffer_move_transfers_ownership)
{
    HeapAllocator heap;
    FramebufferDesc desc{};
    desc.width = 2;
    desc.height = 2;

    auto created = Framebuffer::Create(desc, heap);
    CG_REQUIRE(static_cast<bool>(created));
    Framebuffer framebuffer = std::move(created.Value());
    u8* data = framebuffer.Data();

    Framebuffer moved = std::move(framebuffer);
    CG_EXPECT_FALSE(framebuffer.IsValid());
    CG_EXPECT_EQ(framebuffer.Data(), nullptr);
    CG_EXPECT_EQ(framebuffer.Width(), i32{0});
    CG_EXPECT_TRUE(moved.IsValid());
    CG_EXPECT_TRUE(moved.Data() == data);

    Framebuffer assignTarget;
    assignTarget = std::move(moved);
    CG_EXPECT_FALSE(moved.IsValid());
    CG_EXPECT_TRUE(assignTarget.Data() == data);

    // Default-constructed framebuffers are inert.
    Framebuffer empty;
    empty.Clear(Color{1.0f, 1.0f, 1.0f, 1.0f});
    empty.SetPixel(0, 0, Color{1.0f, 1.0f, 1.0f, 1.0f});
    const Color emptyPixel = empty.GetPixel(0, 0);
    CG_EXPECT_EQ(emptyPixel.a, 0.0f);
    CG_EXPECT_EQ(empty.DataSizeBytes(), usize{0});

    assignTarget = Framebuffer{};
    CG_EXPECT_EQ(heap.Stats().used, usize{0});
}
