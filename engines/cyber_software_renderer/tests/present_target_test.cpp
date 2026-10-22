#include <cyber/memory/heap_allocator.h>
#include <cyber/software/present_target.h>
#include <cyber/test/test.h>

#include <cstring>
#include <utility>

using namespace cyber::software;
using namespace cyber::memory;
using cyber::math::Color;

namespace
{

Span<u8> View(u8* data, usize size)
{
    return Span<u8>(data, size);
}

}  // namespace

CG_TEST(software_present_byte_size)
{
    CG_EXPECT_EQ(PresentByteSize(4, 3), usize{48});
    CG_EXPECT_EQ(PresentByteSize(1, 1), usize{4});
    CG_EXPECT_EQ(PresentByteSize(0, 3), usize{0});
    CG_EXPECT_EQ(PresentByteSize(3, 0), usize{0});
    CG_EXPECT_EQ(PresentByteSize(-3, 4), usize{0});
}

CG_TEST(software_present_packs_srgb)
{
    HeapAllocator heap;

    FramebufferDesc desc{};
    desc.width = 2;
    desc.height = 1;
    desc.format = PixelFormat::RGBA32F;
    auto created = Framebuffer::Create(desc, heap);
    CG_REQUIRE(static_cast<bool>(created));
    Framebuffer source = std::move(created.Value());
    source.SetPixel(0, 0, Color{0.5f, 0.5f, 0.5f, 1.0f});
    source.SetPixel(1, 0, Color{0.0f, 1.0f, 0.25f, 0.0f});

    u8 destination[8] = {};
    CG_EXPECT_TRUE(static_cast<bool>(PackPresentPixels(source, View(destination, 8))));
    CG_EXPECT_EQ(destination[0], u8{188});
    CG_EXPECT_EQ(destination[1], u8{188});
    CG_EXPECT_EQ(destination[2], u8{188});
    CG_EXPECT_EQ(destination[3], u8{255});

    CG_EXPECT_EQ(destination[4], u8{0});
    CG_EXPECT_EQ(destination[5], u8{255});
    CG_EXPECT_EQ(destination[6], EncodeSrgb8(0.25f));
    CG_EXPECT_EQ(destination[7], u8{0});
}

CG_TEST(software_present_packs_unorm_zero_and_one)
{
    HeapAllocator heap;

    FramebufferDesc desc{};
    desc.width = 2;
    desc.height = 1;
    auto created = Framebuffer::Create(desc, heap);
    CG_REQUIRE(static_cast<bool>(created));
    Framebuffer source = std::move(created.Value());
    source.Clear(Color{0.0f, 0.0f, 0.0f, 1.0f});
    source.SetPixel(1, 0, Color{1.0f, 1.0f, 1.0f, 1.0f});

    u8 destination[8] = {};
    CG_REQUIRE(static_cast<bool>(PackPresentPixels(source, View(destination, 8))));
    CG_EXPECT_EQ(destination[0], u8{0});
    CG_EXPECT_EQ(destination[3], u8{255});
    CG_EXPECT_EQ(destination[4], u8{255});
    CG_EXPECT_EQ(destination[7], u8{255});
}

CG_TEST(software_present_stride_and_determinism)
{
    HeapAllocator heap;

    FramebufferDesc desc{};
    desc.width = 2;
    desc.height = 2;
    desc.format = PixelFormat::RGBA32F;
    auto created = Framebuffer::Create(desc, heap);
    CG_REQUIRE(static_cast<bool>(created));
    Framebuffer source = std::move(created.Value());
    for (i32 y = 0; y < 2; ++y)
    {
        for (i32 x = 0; x < 2; ++x)
        {
            source.SetPixel(
                x, y, Color{static_cast<f32>(x) * 0.25f, static_cast<f32>(y) * 0.5f, 0.1f, 1.0f});
        }
    }

    // stride 16 for a two-pixel row leaves eight padding bytes per row.
    u8 padded[32];
    std::memset(padded, 0xAA, sizeof(padded));
    CG_REQUIRE(static_cast<bool>(PackPresentPixels(source, View(padded, sizeof(padded)), 16)));
    for (i32 row = 0; row < 2; ++row)
    {
        for (usize index = 8; index < 16; ++index)
        {
            CG_EXPECT_EQ(padded[static_cast<usize>(row) * 16 + index], u8{0xAA});
        }
    }

    u8 tightA[16] = {};
    u8 tightB[16] = {};
    CG_REQUIRE(static_cast<bool>(PackPresentPixels(source, View(tightA, sizeof(tightA)))));
    CG_REQUIRE(static_cast<bool>(PackPresentPixels(source, View(tightB, sizeof(tightB)))));
    CG_EXPECT_TRUE(std::memcmp(tightA, tightB, sizeof(tightA)) == 0);
    CG_EXPECT_TRUE(std::memcmp(tightA, padded, 8) == 0);
    CG_EXPECT_TRUE(std::memcmp(tightA + 8, padded + 16, 8) == 0);
}

CG_TEST(software_present_rejects_invalid_input)
{
    HeapAllocator heap;

    Framebuffer invalid;
    u8 destination[16] = {};
    const auto invalidSource = PackPresentPixels(invalid, View(destination, sizeof(destination)));
    CG_EXPECT_FALSE(static_cast<bool>(invalidSource));
    CG_EXPECT_TRUE(invalidSource.Error().code == cyber::core::ErrorCode::InvalidArgument);

    FramebufferDesc desc{};
    desc.width = 2;
    desc.height = 1;
    auto created = Framebuffer::Create(desc, heap);
    CG_REQUIRE(static_cast<bool>(created));
    Framebuffer source = std::move(created.Value());

    const auto nullDestination = PackPresentPixels(source, Span<u8>());
    CG_EXPECT_FALSE(static_cast<bool>(nullDestination));

    const auto smallDestination = PackPresentPixels(source, View(destination, 7));
    CG_EXPECT_FALSE(static_cast<bool>(smallDestination));

    const auto smallStride = PackPresentPixels(source, View(destination, sizeof(destination)), 6);
    CG_EXPECT_FALSE(static_cast<bool>(smallStride));
    CG_EXPECT_TRUE(smallStride.Error().code == cyber::core::ErrorCode::InvalidArgument);
}
